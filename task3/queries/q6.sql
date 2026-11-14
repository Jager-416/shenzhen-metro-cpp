SELECT s.name_zh AS chinese_name, d.name_en AS district, e.exit_code AS exit, lm.name
FROM landmark lm JOIN exit_landmark el USING (landmark_id)
JOIN station_exit e USING (exit_id) JOIN station s USING (station_id)
JOIN district d USING (district_id)
WHERE lm.name LIKE $1
ORDER BY s.name_en, s.station_id, e.exit_code, lm.name;
