-- PostgreSQL DDL only. Run in an empty project database/schema.
CREATE TABLE district (
    district_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name_en text NOT NULL UNIQUE
);
CREATE TABLE metro_line (
    line_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name_zh text NOT NULL UNIQUE,
    name_en text NOT NULL UNIQUE
);
CREATE TABLE station (
    station_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    district_id integer NOT NULL REFERENCES district(district_id),
    name_zh text NOT NULL,
    name_en text NOT NULL,
    latitude numeric(9,6),
    longitude numeric(9,6),
    CONSTRAINT station_name_district_key UNIQUE (name_zh, district_id),
    CONSTRAINT station_latitude_check CHECK (latitude BETWEEN -90 AND 90),
    CONSTRAINT station_longitude_check CHECK (longitude BETWEEN -180 AND 180),
    CONSTRAINT station_coordinates_check CHECK ((latitude IS NULL) = (longitude IS NULL))
);
CREATE TABLE line_stop (
    line_id integer NOT NULL REFERENCES metro_line(line_id),
    station_id integer NOT NULL REFERENCES station(station_id),
    position integer NOT NULL CHECK (position > 0),
    status text NOT NULL CHECK (status IN ('Operation', 'Construction')),
    opening_year smallint NOT NULL CHECK (opening_year > 0),
    first_train time,
    last_train time,
    PRIMARY KEY (line_id, station_id),
    CONSTRAINT line_stop_position_key UNIQUE (line_id, position)
        DEFERRABLE INITIALLY IMMEDIATE
);
CREATE TABLE station_exit (
    exit_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    station_id integer NOT NULL REFERENCES station(station_id),
    exit_code text NOT NULL,
    CONSTRAINT station_exit_code_key UNIQUE (station_id, exit_code)
);
CREATE TABLE landmark (
    landmark_id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name text NOT NULL UNIQUE
);
CREATE TABLE exit_landmark (
    exit_id integer NOT NULL REFERENCES station_exit(exit_id),
    landmark_id integer NOT NULL REFERENCES landmark(landmark_id),
    PRIMARY KEY (exit_id, landmark_id)
);
