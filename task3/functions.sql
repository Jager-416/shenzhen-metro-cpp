-- Returns lower-order neighbour first, then higher-order neighbour, like the PDF.
CREATE OR REPLACE FUNCTION get_station_n_neighbor(station_name text, line_name text, n integer)
RETURNS SETOF text
LANGUAGE plpgsql STABLE AS $$
DECLARE
    station_names text[];
    current_index integer;
    found_count integer;
BEGIN
    IF station_name IS NULL OR line_name IS NULL OR n IS NULL OR n < 0 THEN
        RAISE EXCEPTION 'Station, line and nonnegative n are required' USING ERRCODE = '22023';
    END IF;
    SELECT array_agg(s.name_en ORDER BY ls.position)
    INTO station_names
    FROM line_stop ls JOIN metro_line l USING (line_id) JOIN station s USING (station_id)
    WHERE l.name_en = line_name;
    SELECT count(*), min(i) INTO found_count, current_index
    FROM unnest(station_names) WITH ORDINALITY AS t(name, i) WHERE name = station_name;
    IF found_count <> 1 THEN
        RAISE EXCEPTION 'Expected one station % on line %, found %', station_name, line_name, found_count
            USING ERRCODE = '22023';
    END IF;
    RETURN NEXT CASE WHEN current_index::bigint - n >= 1 THEN station_names[current_index - n] END;
    RETURN NEXT CASE WHEN current_index::bigint + n <= cardinality(station_names) THEN station_names[current_index + n] END;
END;
$$;
