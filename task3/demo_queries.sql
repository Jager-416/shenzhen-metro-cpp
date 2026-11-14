-- Q1
SELECT count(*) AS station_count FROM station;

-- Q2
SELECT count(*) AS line_count FROM metro_line;

-- Q3
SELECT d.name_en AS district, count(*) AS station_count
FROM station s JOIN district d USING (district_id)
WHERE EXISTS (SELECT 1 FROM line_stop ls WHERE ls.station_id = s.station_id AND ls.status = 'Operation')
GROUP BY d.name_en ORDER BY d.name_en;

-- Q4
SELECT ls.opening_year, count(*) AS station_count
FROM line_stop ls JOIN metro_line l USING (line_id)
WHERE l.name_en = 'Line 5'
GROUP BY ls.opening_year ORDER BY ls.opening_year;

-- Q5
WITH counts AS (
    SELECT s.station_id, s.name_en, count(e.exit_id) AS exit_count
    FROM line_stop ls JOIN metro_line l USING (line_id)
    JOIN station s USING (station_id) LEFT JOIN station_exit e USING (station_id)
    WHERE l.name_en = 'Line 1'
    GROUP BY s.station_id, s.name_en
)
SELECT name_en AS station_name_en, exit_count FROM counts
WHERE exit_count = (SELECT max(exit_count) FROM counts)
ORDER BY name_en;

-- Q6
SELECT s.name_zh AS chinese_name, d.name_en AS district, e.exit_code AS exit, lm.name
FROM landmark lm JOIN exit_landmark el USING (landmark_id)
JOIN station_exit e USING (exit_id) JOIN station s USING (station_id)
JOIN district d USING (district_id)
WHERE lm.name LIKE '塘朗%'
ORDER BY s.name_en, s.station_id, e.exit_code, lm.name;

-- Q7
SELECT get_station_n_neighbor('Mawan', 'Line 5', 2);

-- Q7 boundary example
SELECT get_station_n_neighbor('Mawan', 'Line 5', 5);
