SELECT ls.opening_year, count(*) AS station_count
FROM line_stop ls JOIN metro_line l USING (line_id)
WHERE l.name_en = $1
GROUP BY ls.opening_year ORDER BY ls.opening_year;
