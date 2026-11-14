WITH counts AS (
    SELECT s.station_id, s.name_en, count(e.exit_id) AS exit_count
    FROM line_stop ls JOIN metro_line l USING (line_id)
    JOIN station s USING (station_id) LEFT JOIN station_exit e USING (station_id)
    WHERE l.name_en = $1
    GROUP BY s.station_id, s.name_en
)
SELECT name_en AS station_name_en, exit_count FROM counts
WHERE exit_count = (SELECT max(exit_count) FROM counts)
ORDER BY name_en;
