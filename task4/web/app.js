'use strict';
const $ = (selector, parent = document) => parent.querySelector(selector);
const $$ = (selector, parent = document) => [...parent.querySelectorAll(selector)];
const escapeHtml = value => String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
const state = {stations: [], lines: [], districts: [], landmarks: [], lineDetails: new Map(), mode: 'station', stationId: null, lineId: null, query: 3, editor: null, mapZoom: 1, route: null, routeVersion: 0, networkVersion: 0, cacheVersion: 0, stationVersion: 0, lineVersion: 0, queryVersion: 0};
const palette = ['#da972b','#bc6436','#82a740','#589ba0','#8578aa','#64b1a0','#4b83b1','#b96b8e','#7f9b7c','#a4ab57','#a45d78','#9a7d57','#db7c51','#7683ab','#629482'];
const color = id => palette[(Number(id) - 1) % palette.length];
const stationLabel = station => `${station.name_zh} · ${station.name_en} · ${station.district}`;
const landmarkLabel = landmark => landmark.name;
const lineChip = line => `<span class="line-chip" style="--line-color:${color(line.line_id)}">${escapeHtml(line.name_zh)}</span>`;
const statusTag = status => `<span class="status-tag ${status === 'Operation' ? '' : 'planned'}">${status === 'Operation' ? '运营中' : '建设中'}</span>`;

function errorMessage(error) {
  const messages = {
    'Failed to fetch': '连接失败，请检查服务是否已启动。',
    'No connected route under the selected operating-status filter': '当前条件下没有连通路线。可尝试包含在建线路。',
    'A station is already on this line': '选择的站点已在线路中，请先移除再调整位置。',
    'Batch contains duplicate station ids': '同一批次不能重复添加站点。',
    'Name, identity or line position already exists': '名称或站点身份已存在，请检查后重试。',
    'Landmark has no associated exits': '该地标当前没有关联的站点出口。',
    'Database unavailable or schema not initialized': '数据库暂不可用，请检查服务后刷新。',
    'Both coordinates must be present or both must be null': '经纬度需要同时填写或同时留空。'
  };
  return messages[error.message] || error.message || '操作未完成，请重试。';
}
async function api(path, options = {}) {
  const response = await fetch(path, {headers: {'Content-Type': 'application/json'}, ...options});
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    const detail = Array.isArray(data.detail) ? data.detail.map(item => `${item.loc?.slice(1).join('.') || ''} ${item.msg}`).join('；') : data.detail;
    throw new Error(detail || `请求失败（${response.status}）`);
  }
  return data;
}
function errorAt(selector, error) {
  const target = $(selector);
  target.textContent = errorMessage(error);
  target.hidden = false;
}
let toastTimer;
function toast(message) {
  $('#toast').textContent = message;
  $('#toast').hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { $('#toast').hidden = true; }, 3500);
}
function loading(button, active, label) {
  button.disabled = active;
  button.textContent = active ? '正在处理…' : label;
}
function emptyTable(columns, message) {
  return `<tr><td colspan="${columns}" class="simple-empty">${escapeHtml(message)}</td></tr>`;
}
function stationOptions(selected = '') {
  return state.stations.map(station => `<option value="${escapeHtml(stationLabel(station))}"${station.station_id === Number(selected) ? ' selected' : ''}></option>`).join('');
}
function stationId(input) {
  const label = input.value.trim();
  const exact = state.stations.filter(station => stationLabel(station) === label);
  const matches = exact.length ? exact : state.stations.filter(station => station.name_zh === label || station.name_en === label);
  if (matches.length === 1) return matches[0].station_id;
  throw new Error(matches.length ? '存在同名站，请从建议列表中选择包含行政区的完整站名。' : '请选择建议列表中的有效站点。');
}
function landmarkId(input) {
  const found = state.landmarks.find(landmark => landmark.name === input.value.trim());
  if (!found) throw new Error('请选择建议列表中的有效地标。');
  return found.landmark_id;
}
async function refreshData() {
  const previousDistrict = $('#district-filter').value;
  const [stations, lines, districts, landmarks] = await Promise.all([
    api('/api/stations?limit=2000'), api('/api/lines'), api('/api/districts'), api('/api/landmarks?limit=3000')
  ]);
  Object.assign(state, {stations, lines, districts, landmarks});
  state.cacheVersion++;
  state.lineDetails.clear();
  $('#station-options').innerHTML = stationOptions();
  $('#landmark-options').innerHTML = landmarks.map(row => `<option value="${escapeHtml(landmarkLabel(row))}"></option>`).join('');
  $('#district-options').innerHTML = districts.map(row => `<option value="${escapeHtml(row.name_en)}"></option>`).join('');
  $('#district-filter').innerHTML = '<option value="">全部行政区</option>' + districts.map(row => `<option value="${escapeHtml(row.name_en)}">${escapeHtml(row.name_en)}</option>`).join('');
  $('#district-filter').value = previousDistrict;
  $('#station-count').textContent = stations.length;
  $('#line-count').textContent = lines.length;
  $('#connection').textContent = '数据库已连接';
  $('#connection').classList.remove('offline');
  $('#global-error').hidden = true;
  renderStations();
  renderLines();
  loadNetwork().catch(error => { $('#map-caption').textContent = '线网加载失败'; console.warn(error); });
}
function switchView() {
  const key = location.hash.slice(1);
  const view = ['journey', 'stations', 'lines', 'queries'].includes(key) ? key : 'journey';
  $$('.view').forEach(section => { section.hidden = section.id !== `view-${view}`; });
  $$('nav a').forEach(link => { const active = link.dataset.view === view; link.classList.toggle('active', active); if (active) link.setAttribute('aria-current', 'page'); else link.removeAttribute('aria-current'); });
  $('#view-name').textContent = {journey: '出行查询', stations: '站点管理', lines: '线路管理', queries: '数据查询'}[view];
  if (view === 'journey') requestAnimationFrame(drawNetwork);
}
function setMode(mode) {
  state.mode = mode;
  state.routeVersion++;
  state.route = null;
  $$('[data-mode]').forEach(button => {
    button.classList.toggle('active', button.dataset.mode === mode);
    button.setAttribute('aria-pressed', String(button.dataset.mode === mode));
  });
  for (const name of ['origin', 'destination']) {
    const input = $(`#${name}`);
    input.value = '';
    input.setAttribute('list', mode === 'station' ? 'station-options' : 'landmark-options');
    input.placeholder = mode === 'station' ? '输入中文或英文站名' : '输入地标名称，例如医院、学校';
  }
  $('#origin-label').textContent = mode === 'station' ? '出发站点' : '出发地标';
  $('#destination-label').textContent = mode === 'station' ? '到达站点' : '到达地标';
  $('.quick-trips').hidden = mode !== 'station';
  $('#journey-error').hidden = true;
  loading($('#journey-submit'), false, '查询路线 ↗');
  $('#journey-result').innerHTML = '<div class="empty-result"><span class="empty-symbol">↗</span><div><h3>把目的地交给我们</h3><p>选择起点与终点，查看路线、换乘和票价。</p></div></div>';
  drawNetwork();
}
async function submitJourney(event) {
  event.preventDefault();
  const version = ++state.routeVersion;
  const mode = state.mode;
  $('#journey-error').hidden = true;
  loading($('#journey-submit'), true);
  try {
    const resolve = mode === 'station' ? stationId : landmarkId;
    const origin = resolve($('#origin'));
    const destination = resolve($('#destination'));
    const params = new URLSearchParams({origin, destination, preference: $('#preference').value, include_construction: $('#include-construction').checked});
    const result = await api(`/api/${mode === 'station' ? 'routes' : 'landmark-routes'}?${params}`);
    let fare;
    try {
      fare = await api(`/api/fares?${new URLSearchParams({origin: result.origin_station_id, destination: result.destination_station_id})}`);
    } catch (error) { fare = {amount_cny: null, note: result.includes_construction ? '包含在建区段，当前无运营票价。' : errorMessage(error)}; }
    if (version !== state.routeVersion) return;
    state.route = result;
    state.mapZoom = 1;
    renderRoute(result, fare);
    drawNetwork();
  } catch (error) {
    if (version === state.routeVersion) {
      state.route = null;
      $('#journey-result').innerHTML = '<div class="empty-result"><span class="empty-symbol">↗</span><div><h3>暂未找到路线</h3><p>请检查站点与查询条件后重试。</p></div></div>';
      errorAt('#journey-error', error);
      drawNetwork();
    }
  } finally {
    if (version === state.routeVersion) loading($('#journey-submit'), false, '查询路线 ↗');
  }
}
function renderRoute(result, fare) {
  const lookup = new Map(result.stations.map(station => [station.station_id, station]));
  const first = result.stations[0], last = result.stations.at(-1);
  const fareLabel = {published_table: '公布单程票价', coordinate_estimate: '坐标估算票价', user_supplied_distance: '按里程计价'}[fare.kind] || '暂无票价';
  const segments = result.segments.length ? result.segments.map(segment => `<div class="route-segment">${lineChip(segment)}<div class="segment-names">${segment.station_ids.map(id => `<b style="font-weight:400">${escapeHtml(lookup.get(id)?.name_zh || id)}</b>`).join('<span>→</span>')}</div></div>`).join('') : '<div class="subtle">起点与终点为同一站，无需乘车。</div>';
  const fareNote = fare.kind === 'published_table' ? `普通车厢单程全价 · ${escapeHtml(fare.publication_date)} 公布 <a href="${escapeHtml(fare.source)}" target="_blank" rel="noopener noreferrer">查看票价表 ↗</a>` : escapeHtml(fare.note || '');
  const exits = result.origin_exits ? `<div class="exit-directions"><div><b>起点 · 进入车站</b>${result.origin_exits.map(exit => `${escapeHtml(exit.name_zh)} ${escapeHtml(exit.exit_code)} 口`).join(' / ')}</div><div><b>终点 · 离开车站</b>${result.destination_exits.map(exit => `${escapeHtml(exit.name_zh)} ${escapeHtml(exit.exit_code)} 口`).join(' / ')}</div></div>` : '';
  $('#journey-result').innerHTML = `<div class="card route-card"><div class="route-top"><div class="route-stations">${escapeHtml(first.name_zh)}<span>→</span>${escapeHtml(last.name_zh)}</div><div class="route-statistics"><div><b>${result.stops}</b><small>站</small></div><div><b>${result.transfers}</b><small>次换乘</small></div><div><b>${fare.amount_cny == null ? '—' : `¥${fare.amount_cny}`}</b><span class="fare-label">${fareLabel}</span></div></div></div><div class="route-timeline">${segments}</div>${exits}<p class="route-footnote">${fareNote}${result.includes_construction ? ' 路线允许经过在建区段。' : ''}</p></div>`;
}
async function lineDetail(id) {
  if (state.lineDetails.has(id)) return state.lineDetails.get(id);
  const version = state.cacheVersion;
  const result = await api(`/api/lines/${id}`);
  if (version === state.cacheVersion) state.lineDetails.set(id, result);
  return result;
}
async function loadNetwork() {
  const version = ++state.networkVersion;
  const lines = await Promise.all(state.lines.map(line => lineDetail(line.line_id)));
  if (version !== state.networkVersion) return;
  state.network = lines;
  drawNetwork();
}
function drawNetwork() {
  const canvas = $('#network-map');
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = rect.width * ratio;
  canvas.height = rect.height * ratio;
  const ctx = canvas.getContext('2d');
  ctx.scale(ratio, ratio);
  const width = rect.width, height = rect.height;
  ctx.fillStyle = '#edf2e7'; ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = '#dce5d54d'; ctx.lineWidth = 1;
  for (let x = 0; x < width; x += 32) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke(); }
  for (let y = 0; y < height; y += 32) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke(); }
  const byId = new Map(state.stations.map(row => [row.station_id, row]));
  const all = state.stations.filter(row => row.latitude != null && row.longitude != null);
  if (!all.length) return;
  const chosen = state.route?.stations.filter(row => row.latitude != null && row.longitude != null) || [];
  const bounds = chosen.length >= 2 ? chosen : all;
  const xs = bounds.map(row => Number(row.longitude)), ys = bounds.map(row => Number(row.latitude));
  const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
  const centerX = (minX + maxX) / 2, centerY = (minY + maxY) / 2;
  const xSpan = Math.max(maxX - minX, .03), ySpan = Math.max(maxY - minY, .03);
  const geographicX = Math.cos(centerY * Math.PI / 180);
  const scale = Math.min((width - 100) / (xSpan * geographicX), (height - 130) / ySpan) * state.mapZoom;
  const point = row => [width / 2 + (Number(row.longitude) - centerX) * geographicX * scale, height / 2 + 8 - (Number(row.latitude) - centerY) * scale];
  ctx.save(); ctx.beginPath(); ctx.rect(0, 58, width, height - 98); ctx.clip();
  function edge(a, b, stroke, weight) {
    if (a?.latitude == null || b?.latitude == null) return;
    ctx.beginPath(); ctx.moveTo(...point(a)); ctx.lineTo(...point(b)); ctx.strokeStyle = stroke; ctx.lineWidth = weight; ctx.lineJoin = 'round'; ctx.lineCap = 'round'; ctx.stroke();
  }
  for (const line of state.network || []) {
    for (let i = 1; i < line.stops.length; i++) edge(byId.get(line.stops[i - 1].station_id), byId.get(line.stops[i].station_id), state.route ? '#c4d4bd80' : color(line.line_id) + 'a0', 2.6);
  }
  for (const station of all) {
    const [x, y] = point(station); ctx.beginPath(); ctx.arc(x, y, 2.4, 0, Math.PI * 2); ctx.fillStyle = '#f9fcf4'; ctx.fill(); ctx.strokeStyle = '#9bad8f'; ctx.lineWidth = .8; ctx.stroke();
  }
  if (state.route) {
    for (const segment of state.route.segments) {
      for (let i = 1; i < segment.station_ids.length; i++) edge(byId.get(segment.station_ids[i - 1]), byId.get(segment.station_ids[i]), color(segment.line_id), 4);
    }
  }
  const labels = state.route ? [state.route.stations[0], state.route.stations.at(-1)] : ['罗湖', '深圳北站', '福田', '前海湾', '宝安中心', '大运'].map(name => all.find(row => row.name_zh === name)).filter(Boolean);
  ctx.font = '11px Inter, sans-serif';
  for (const row of labels) {
    if (row.latitude == null) continue;
    const [x, y] = point(row); if (x < 5 || x > width - 50 || y < 65 || y > height - 45) continue;
    ctx.beginPath(); ctx.arc(x, y, state.route ? 5 : 3.5, 0, Math.PI * 2); ctx.fillStyle = '#315b40'; ctx.fill();
    const labelX = Math.max(9, Math.min(width - ctx.measureText(row.name_zh).width - 10, x + 9));
    const labelY = Math.max(73, y - 8);
    ctx.lineWidth = 4; ctx.strokeStyle = '#edf2e7'; ctx.strokeText(row.name_zh, labelX, labelY); ctx.fillStyle = '#456b48'; ctx.fillText(row.name_zh, labelX, labelY);
  }
  ctx.restore();
  $('#map-caption').textContent = state.route ? '所选路线 · 有坐标区段' : '有坐标站点';
}

function renderStations() {
  const text = $('#station-search').value.trim().toLowerCase();
  const district = $('#district-filter').value;
  const rows = state.stations.filter(station => (!district || station.district === district) && `${station.name_zh} ${station.name_en}`.toLowerCase().includes(text));
  $('#station-total').textContent = `${rows.length} 个站点`;
  $('#stations-body').innerHTML = rows.map(row => `<tr data-station="${row.station_id}" class="${row.station_id === state.stationId ? 'selected' : ''}"><td><button type="button" class="text-button station-name" data-action="station-detail" data-id="${row.station_id}">${escapeHtml(row.name_zh)}</button><div class="english-name">${escapeHtml(row.name_en)}</div></td><td>${escapeHtml(row.district)}</td><td><button class="text-button" data-action="station-edit" data-id="${row.station_id}">编辑</button></td></tr>`).join('') || emptyTable(3, '没有匹配的站点，试试其他名称或行政区。');
}
async function showStation(id) {
  const version = ++state.stationVersion;
  state.stationId = id;
  renderStations();
  $('#station-detail').innerHTML = '<div class="loading-text">正在读取站点…</div>';
  try {
    const row = await api(`/api/stations/${id}`);
    if (state.stationId !== id || version !== state.stationVersion) return;
    $('#station-detail').innerHTML = `<div class="detail-title"><div><h2>${escapeHtml(row.name_zh)}</h2><p>${escapeHtml(row.name_en)}</p></div><span class="subtle">${escapeHtml(row.district)}</span></div><div class="detail-actions"><button class="button secondary small" data-action="station-edit" data-id="${id}">编辑站点</button><button class="button secondary small" data-action="route-from" data-id="${id}">从这里出发 ↗</button><button class="text-button destructive" data-action="station-delete" data-id="${id}">删除站点</button></div><div class="detail-label">经过线路 · ${row.lines.length}</div><div class="detail-lines">${row.lines.map(line => `<button class="text-button" data-action="open-line" data-id="${line.line_id}">${lineChip(line)}</button>`).join('') || '<span class="subtle">尚未加入线路</span>'}</div><div class="detail-facts"><div>纬度 ${row.latitude ?? '暂无'}</div><div>经度 ${row.longitude ?? '暂无'}</div></div><div class="detail-label">出入口与周边地标 · ${row.exits.length}</div>${row.exits.map(exit => `<div class="exit-item"><span class="exit-code">${escapeHtml(exit.exit_code)}</span><div>${exit.landmarks.map(escapeHtml).join('、') || '暂无地标信息'}</div></div>`).join('') || '<p class="subtle">暂无出口记录</p>'}`;
  } catch (error) { if (state.stationId === id && version === state.stationVersion) $('#station-detail').innerHTML = `<div class="inline-error">${escapeHtml(errorMessage(error))}</div>`; }
}
function renderLines() {
  const text = $('#line-search').value.trim().toLowerCase();
  const lines = state.lines.filter(row => `${row.name_zh} ${row.name_en}`.toLowerCase().includes(text));
  $('#lines-list').innerHTML = lines.map(line => `<button class="line-choice ${line.line_id === state.lineId ? 'selected' : ''}" data-action="line-detail" data-id="${line.line_id}"><i class="line-color" style="--line-color:${color(line.line_id)}"></i><span><b>${escapeHtml(line.name_zh)}</b><small>${escapeHtml(line.name_en)}</small></span><span class="line-size">${line.station_count} 站</span></button>`).join('') || '<div class="simple-empty">未找到线路</div>';
}
async function showLine(id) {
  const version = ++state.lineVersion;
  state.lineId = id;
  renderLines();
  $('#line-detail').innerHTML = '<div class="loading-text">正在读取线路…</div>';
  try {
    const line = await lineDetail(id);
    if (state.lineId !== id || version !== state.lineVersion) return;
    $('#line-detail').innerHTML = `<div class="line-content-header"><div class="detail-title"><div><h2>${escapeHtml(line.name_zh)}</h2><p>${escapeHtml(line.name_en)} · ${line.stops.length} 个站点</p></div>${lineChip(line)}</div><div class="detail-actions"><button class="button primary small" data-action="stop-add" data-id="${id}">＋ 插入站点</button><button class="button secondary small" data-action="line-edit" data-id="${id}">编辑线路</button><button class="text-button destructive" data-action="line-delete" data-id="${id}">删除线路</button></div></div><div class="table-scroll line-stops"><table><thead><tr><th>站序 / 名称</th><th>状态 / 年份</th><th>首末班</th><th>操作</th></tr></thead><tbody>${line.stops.map(stop => `<tr><td><div class="stop-position"><i>${stop.position}</i><div>${escapeHtml(stop.name_zh)}<div class="english-name">${escapeHtml(stop.name_en)}</div></div></div></td><td>${statusTag(stop.status)}<div class="english-name">${stop.opening_year}</div></td><td><span class="subtle">${stop.first_train?.slice(0, 5) || '—'}<br>${stop.last_train?.slice(0, 5) || '—'}</span></td><td><button class="text-button" data-action="stop-edit" data-id="${stop.station_id}" data-line="${id}">编辑</button><button class="text-button destructive" data-action="stop-remove" data-id="${stop.station_id}" data-line="${id}">移除</button></td></tr>`).join('') || emptyTable(4, '这条线路还没有站点，点击“插入站点”开始。')}</tbody></table></div>`;
  } catch (error) { if (state.lineId === id && version === state.lineVersion) $('#line-detail').innerHTML = `<div class="inline-error">${escapeHtml(errorMessage(error))}</div>`; }
}

function field(label, name, value = '', options = {}) {
  const {type = 'text', required = true, full = false, attrs = ''} = options;
  return `<label class="${full ? 'full' : ''}">${escapeHtml(label)}<input name="${name}" type="${type}" value="${escapeHtml(value ?? '')}" ${required ? 'required' : ''} ${attrs}></label>`;
}
function metadataFields(row = {}) {
  return `<label>运营状态<select name="status"><option value="Operation" ${row.status !== 'Construction' ? 'selected' : ''}>运营中</option><option value="Construction" ${row.status === 'Construction' ? 'selected' : ''}>建设中</option></select></label>${field('开通 / 预计开通年份', 'opening_year', row.opening_year || 2026, {type: 'number', attrs: 'min="1" max="32767"'})}${field('首班时间', 'first_train', row.first_train?.slice(0, 5), {type: 'time', required: false})}${field('末班时间', 'last_train', row.last_train?.slice(0, 5), {type: 'time', required: false})}`;
}
function openEditor(context, title, html) {
  state.editor = context;
  $('#editor-title').textContent = title;
  $('#editor-fields').innerHTML = html;
  $('#editor-error').hidden = true;
  loading($('#editor-submit'), false, '保存');
  $('#editor').showModal();
}
function stationEditor(id) {
  const row = state.stations.find(item => item.station_id === id) || {};
  openEditor({type: 'station', id}, id ? '编辑站点' : '新增站点', `<div class="editor-grid">${field('中文名称', 'name_zh', row.name_zh, {attrs: 'maxlength="200"'})}${field('英文名称', 'name_en', row.name_en, {attrs: 'maxlength="200"'})}${field('行政区英文名', 'district', row.district, {full: true, attrs: 'list="district-options" maxlength="200"'})}${field('纬度', 'latitude', row.latitude, {type: 'number', required: false, attrs: 'min="-90" max="90" step="any"'})}${field('经度', 'longitude', row.longitude, {type: 'number', required: false, attrs: 'min="-180" max="180" step="any"'})}</div><p class="field-hint">经纬度可同时留空。同名站通过行政区区分。</p>`);
}
function lineEditor(id) {
  const row = state.lines.find(item => item.line_id === id) || {};
  openEditor({type: 'line', id}, id ? '编辑线路' : '新增线路', `<div class="editor-grid">${field('中文名称', 'name_zh', row.name_zh, {attrs: 'maxlength="200"'})}${field('英文名称', 'name_en', row.name_en, {attrs: 'maxlength="200"'})}</div>`);
}
async function batchEditor(lineId) {
  const line = await lineDetail(lineId);
  openEditor({type: 'batch', lineId}, `向${line.name_zh}插入站点`, `${field('插入位置', 'position', line.stops.length + 1, {type: 'number', attrs: `min="1" max="${line.stops.length + 1}"`})}<p class="field-hint">选择位置后，原有站点顺延。列表顺序即插入顺序。</p><div id="batch-items"></div><button type="button" class="button secondary batch-add" data-action="batch-more">＋ 再添加一个站点</button>`);
  addBatchItem();
}
function addBatchItem() {
  const node = document.createElement('div'); node.className = 'batch-item';
  node.innerHTML = `<div class="batch-item-header"><span>待插入站点</span><button type="button" class="text-button destructive" data-action="batch-remove">移除</button></div><div class="editor-grid">${field('站点', 'station', '', {full: true, attrs: 'list="station-options" placeholder="输入并选择已有站点"'})}${metadataFields()}</div>`;
  $('#batch-items').append(node);
}
async function stopEditor(lineId, stationId) {
  const line = await lineDetail(lineId);
  const row = line.stops.find(item => item.station_id === stationId);
  if (!row) throw new Error('该站点已从线路移除，请刷新。');
  openEditor({type: 'stop', lineId, stationId}, `${row.name_zh} · 线路信息`, `<div class="editor-grid">${metadataFields(row)}</div>`);
}
function formValues(parent) {
  return Object.fromEntries($$('input[name], select[name]', parent).map(input => [input.name, input.value.trim()]));
}
function stopPayload(values) {
  return {status: values.status, opening_year: Number(values.opening_year), first_train: values.first_train || null, last_train: values.last_train || null};
}
async function submitEditor(event) {
  event.preventDefault();
  const context = state.editor;
  const values = formValues($('#editor-fields'));
  $('#editor-error').hidden = true;
  loading($('#editor-submit'), true);
  let saved;
  try {
    if (context.type === 'station') {
      const coordinates = {latitude: values.latitude === '' ? null : Number(values.latitude), longitude: values.longitude === '' ? null : Number(values.longitude)};
      if ((coordinates.latitude == null) !== (coordinates.longitude == null)) throw new Error('经纬度需要同时填写或同时留空。');
      saved = await api(`/api/stations${context.id ? '/' + context.id : ''}`, {method: context.id ? 'PATCH' : 'POST', body: JSON.stringify({name_zh: values.name_zh, name_en: values.name_en, district: values.district, ...coordinates})});
    } else if (context.type === 'line') {
      saved = await api(`/api/lines${context.id ? '/' + context.id : ''}`, {method: context.id ? 'PATCH' : 'POST', body: JSON.stringify(values)});
    } else if (context.type === 'batch') {
      const items = $$('.batch-item');
      if (!items.length) throw new Error('请至少添加一个站点。');
      const stops = items.map(item => ({station_id: stationId($('[name=station]', item)), ...stopPayload(formValues(item))}));
      saved = await api(`/api/lines/${context.lineId}/stops`, {method: 'POST', body: JSON.stringify({position: Number(values.position), stops})});
    } else {
      saved = await api(`/api/lines/${context.lineId}/stops/${context.stationId}`, {method: 'PATCH', body: JSON.stringify(stopPayload(values))});
    }
    $('#editor').close();
    toast('已保存');
    await refreshAfterWrite();
    if (context.type === 'station') await showStation(saved.station_id);
    else if (context.type === 'line') await showLine(saved.line_id);
    else await showLine(context.lineId);
  } catch (error) {
    if ($('#editor').open) errorAt('#editor-error', error);
    else errorAt('#global-error', new Error('保存已完成，但刷新失败。请点击右上角刷新。'));
  } finally { loading($('#editor-submit'), false, '保存'); }
}
function confirmDelete(title, message) {
  $('#confirm-title').textContent = title;
  $('#confirm-message').textContent = message;
  $('#confirmation').showModal();
  return new Promise(resolve => {
    const dialog = $('#confirmation');
    const finish = value => { dialog.removeEventListener('cancel', cancel); $('#confirm-cancel').onclick = null; $('#confirm-yes').onclick = null; dialog.close(); resolve(value); };
    const cancel = event => { event.preventDefault(); finish(false); };
    $('#confirm-cancel').onclick = () => finish(false);
    $('#confirm-yes').onclick = () => finish(true);
    dialog.addEventListener('cancel', cancel);
  });
}
async function refreshAfterWrite() {
  state.routeVersion++;
  loading($('#journey-submit'), false, '查询路线 ↗');
  $('#journey-error').hidden = true;
  state.route = null;
  $('#journey-result').innerHTML = '<div class="empty-result"><span class="empty-symbol">↗</span><div><h3>线网信息已更新</h3><p>重新查询以获取最新路线。</p></div></div>';
  await refreshData();
  if (state.stationId) await showStation(state.stationId);
  if (state.lineId) await showLine(state.lineId);
}
async function deleteRecord(type, id, lineId) {
  let title, message, path;
  if (type === 'station') {
    const row = state.stations.find(item => item.station_id === id);
    title = `删除${row?.name_zh || '站点'}？`;
    message = '该站点的线路归属、出口和地标关联将一并移除，受影响线路会重新排列站序。';
    path = `/api/stations/${id}`;
  } else if (type === 'line') {
    const row = state.lines.find(item => item.line_id === id);
    title = `删除${row?.name_zh || '线路'}？`;
    message = '线路及其站点归属将被移除，实体站点继续保留。';
    path = `/api/lines/${id}`;
  } else {
    const row = state.stations.find(item => item.station_id === id);
    title = `从线路移除${row?.name_zh || '站点'}？`;
    message = '后续站点会依次前移，站点本身继续保留。';
    path = `/api/lines/${lineId}/stops/${id}`;
  }
  if (!await confirmDelete(title, message)) return;
  await api(path, {method: 'DELETE'});
  if (type === 'station') { state.stationId = null; $('#station-detail').innerHTML = '<div class="panel-empty">◎<h3>站点已删除</h3><p>选择其他站点查看详情。</p></div>'; }
  if (type === 'line') { state.lineId = null; $('#line-detail').innerHTML = '<div class="panel-empty">≋<h3>线路已删除</h3><p>选择其他线路查看站序。</p></div>'; }
  await refreshAfterWrite();
  if (type === 'stop') await showLine(lineId);
  toast(type === 'stop' ? '已移除站点，站序已更新' : '已删除');
}

const queries = [
  {title: '站点总数', description: '统计实体站点数量，换乘站只计一次。'},
  {title: '线路总数', description: '统计数据集中的线路，包括分期与支线。'},
  {title: '各区运营站点', description: '按行政区统计至少有一条运营线路的站点，按行政区名称排序。'},
  {title: '线路开通年份', description: '统计指定线路各开通或预计开通年份的站点数量。'},
  {title: '出口最多的站点', description: '查找指定线路出口最多的站点，并列结果全部保留。'},
  {title: '地标前缀查询', description: '按地标名称前缀查找站点、行政区与出入口。'},
  {title: '前后第 n 站', description: '按线路站序查询前后站点，越界方向返回空值。'}
];
function selectQuery(number) {
  state.queryVersion++;
  state.query = number;
  const query = queries[number - 1];
  $('#query-menu').innerHTML = queries.map((row, index) => `<button class="query-choice ${index + 1 === number ? 'active' : ''}" data-query="${index + 1}"><span>Q${index + 1}</span>${row.title}</button>`).join('');
  $('#query-badge').textContent = `Q${number}`;
  $('#query-title').textContent = query.title;
  $('#query-description').textContent = query.description;
  let html = '';
  if ([4, 5, 7].includes(number)) html += `<label>线路<select name="line_name" id="query-line">${state.lines.map(line => `<option value="${escapeHtml(line.name_en)}" ${line.name_en === (number === 5 ? 'Line 1' : 'Line 5') ? 'selected' : ''}>${escapeHtml(line.name_zh)} · ${escapeHtml(line.name_en)}</option>`).join('')}</select></label>`;
  if (number === 6) html += field('地标前缀', 'prefix', '塘朗', {attrs: 'placeholder="例如 塘朗"'});
  if (number === 7) html += '<label>站点英文名<select name="station_name" id="query-station" required></select></label>' + field('间隔站数 n', 'n', 2, {type: 'number', attrs: 'min="0" max="2147483647"'});
  $('#query-fields').innerHTML = html;
  $('#query-error').hidden = true;
  $('#query-output').innerHTML = '<div class="simple-empty">运行查询后，结果将在这里显示。</div>';
  $('#query-meta').textContent = '等待运行';
  loading($('#query-submit'), false, '运行查询 ↗');
  if (number === 7) updateQueryStations().catch(error => errorAt('#query-error', error));
}
async function updateQueryStations() {
  const chosen = $('#query-line')?.value;
  const line = state.lines.find(item => item.name_en === chosen);
  if (!line) return;
  const detail = await lineDetail(line.line_id);
  if (state.query !== 7 || $('#query-line')?.value !== chosen) return;
  $('#query-station').innerHTML = detail.stops.map(stop => `<option value="${escapeHtml(stop.name_en)}" ${stop.name_en === 'Mawan' ? 'selected' : ''}>${escapeHtml(stop.name_zh)} · ${escapeHtml(stop.name_en)}</option>`).join('');
}
const columnLabels = {station_count: '站点数量', line_count: '线路数量', district: '行政区', opening_year: '开通年份', station_name_en: '英文站名', exit_count: '出口数量', chinese_name: '中文站名', exit: '出口', name: '地标全名', get_station_n_neighbor: '站点英文名', direction: '方向'};
function resultTable(rows) {
  if (!rows.length) return '<div class="simple-empty">没有符合条件的记录。</div>';
  const columns = Object.keys(rows[0]);
  return `<div class="query-output-scroll"><table><thead><tr>${columns.map(key => `<th>${columnLabels[key] || escapeHtml(key)}</th>`).join('')}</tr></thead><tbody>${rows.map(row => `<tr>${columns.map(key => `<td>${row[key] == null ? '<span class="subtle">NULL · 已越界</span>' : escapeHtml(row[key])}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
}
async function submitQuery(event) {
  event.preventDefault();
  const number = state.query;
  const version = ++state.queryVersion;
  const values = formValues($('#query-fields'));
  if (number === 6) { values.pattern = values.prefix + '%'; delete values.prefix; }
  $('#query-error').hidden = true;
  loading($('#query-submit'), true);
  $('#query-meta').textContent = '查询中';
  $('#query-output').innerHTML = '<div class="loading-text">正在查询…</div>';
  const start = performance.now();
  try {
    const rows = await api(`/api/queries/q${number}?${new URLSearchParams(values)}`);
    if (number !== state.query || version !== state.queryVersion) return;
    const display = number === 7 ? rows.map((row, index) => ({direction: index ? '较大站序方向' : '较小站序方向', ...row})) : rows;
    $('#query-output').innerHTML = resultTable(display);
    $('#query-meta').textContent = `${rows.length} 条结果 · ${Math.round(performance.now() - start)} ms`;
  } catch (error) { if (number === state.query && version === state.queryVersion) { errorAt('#query-error', error); $('#query-meta').textContent = '查询失败'; $('#query-output').innerHTML = '<div class="simple-empty">请检查查询条件后重试。</div>'; } }
  finally { if (number === state.query && version === state.queryVersion) loading($('#query-submit'), false, '运行查询 ↗'); }
}

async function action(button) {
  const id = Number(button.dataset.id), lineId = Number(button.dataset.line);
  switch (button.dataset.action) {
    case 'station-detail': return showStation(id);
    case 'station-edit': return stationEditor(id);
    case 'station-delete': return deleteRecord('station', id);
    case 'line-detail': return showLine(id);
    case 'open-line': location.hash = 'lines'; return showLine(id);
    case 'line-edit': return lineEditor(id);
    case 'line-delete': return deleteRecord('line', id);
    case 'stop-add': return batchEditor(id);
    case 'stop-edit': return stopEditor(lineId, id);
    case 'stop-remove': return deleteRecord('stop', id, lineId);
    case 'batch-more': return addBatchItem();
    case 'batch-remove': return button.closest('.batch-item').remove();
    case 'route-from': {
      const station = state.stations.find(row => row.station_id === id);
      if (!station) return;
      setMode('station'); $('#origin').value = stationLabel(station); location.hash = 'journey'; $('#destination').focus(); return;
    }
  }
}
document.addEventListener('click', event => {
  const button = event.target.closest('[data-action]');
  if (button) { action(button).catch(error => { if ($('#editor').open) errorAt('#editor-error', error); else errorAt('#global-error', error); }); return; }
  const row = event.target.closest('tr[data-station]');
  if (row) showStation(Number(row.dataset.station));
});
$$('[data-close]').forEach(button => button.addEventListener('click', () => { if (!$('#editor-submit').disabled) $('#editor').close(); }));
$('#editor').addEventListener('cancel', event => { if ($('#editor-submit').disabled) event.preventDefault(); });
$('#editor-form').addEventListener('submit', submitEditor);
$('#add-station').addEventListener('click', () => stationEditor());
$('#add-line').addEventListener('click', () => lineEditor());
$('#station-search').addEventListener('input', renderStations);
$('#district-filter').addEventListener('change', renderStations);
$('#line-search').addEventListener('input', renderLines);
$('#journey-form').addEventListener('submit', submitJourney);
$('#swap').addEventListener('click', () => { [$('#origin').value, $('#destination').value] = [$('#destination').value, $('#origin').value]; });
$$('[data-mode]').forEach(button => button.addEventListener('click', () => setMode(button.dataset.mode)));
$$('[data-trip]').forEach(button => button.addEventListener('click', () => {
  setMode('station'); const [origin, destination] = button.dataset.trip.split('|');
  const a = state.stations.find(station => station.name_en === origin), b = state.stations.find(station => station.name_en === destination);
  if (!a || !b) return toast('该示例站点已修改，请手动选择。');
  $('#origin').value = stationLabel(a); $('#destination').value = stationLabel(b); $('#journey-form').requestSubmit();
}));
$$('[data-map]').forEach(button => button.addEventListener('click', () => { state.mapZoom = button.dataset.map === 'reset' ? 1 : Math.max(.5, Math.min(4, state.mapZoom * (button.dataset.map === 'in' ? 1.25 : .8))); drawNetwork(); }));
$('#query-menu').addEventListener('click', event => { const button = event.target.closest('[data-query]'); if (button) selectQuery(Number(button.dataset.query)); });
$('#query-fields').addEventListener('change', event => { if (event.target.id === 'query-line' && state.query === 7) updateQueryStations().catch(error => errorAt('#query-error', error)); });
$('#query-form').addEventListener('submit', submitQuery);
$('#refresh').addEventListener('click', async () => {
  $('#refresh').disabled = true;
  try { await refreshAfterWrite(); if (state.stationId) await showStation(state.stationId); if (state.lineId) await showLine(state.lineId); selectQuery(state.query); toast('数据已刷新'); }
  catch (error) { $('#connection').textContent = '连接失败'; $('#connection').classList.add('offline'); errorAt('#global-error', error); }
  finally { $('#refresh').disabled = false; }
});
window.addEventListener('hashchange', switchView);
new ResizeObserver(drawNetwork).observe($('#network-map'));
async function boot() {
  switchView();
  try { await refreshData(); selectQuery(3); }
  catch (error) {
    $('#connection').textContent = '连接失败'; $('#connection').classList.add('offline'); errorAt('#global-error', error);
  }
}
boot();
