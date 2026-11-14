SELECT d.name_en AS district, count(*) AS station_count
FROM station s JOIN district d USING (district_id)
WHERE EXISTS (SELECT 1 FROM line_stop ls WHERE ls.station_id = s.station_id AND ls.status = 'Operation')
GROUP BY d.name_en ORDER BY d.name_en;
