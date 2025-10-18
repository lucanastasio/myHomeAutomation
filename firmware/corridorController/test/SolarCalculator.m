
SUNRISESET_STD_ALTITUDE = -0.8333;
CIVIL_DAWNDUSK_STD_ALTITUDE = -6.0;
NAUTICAL_DAWNDUSK_STD_ALTITUDE = -12.0;
ASTRONOMICAL_DAWNDUSK_STD_ALTITUDE = -18.0;

function jd = JulianDay(utc)
    if nargin == 1
        jd.JD = floor(utc / 86400) + 2440587.5;
        jd.m = mod(utc, 86400) / 86400.0;
    elseif nargin == 6
        Y = utc; M = varargin{1}; D = varargin{2}; hour = varargin{3}; minute = varargin{4}; second = varargin{5};
        jd.JD = 367 * Y - floor(7 * (Y + (M + 9) / 12) / 4) + floor(275 * M / 9) + D + 1721013.5;
        jd.m = (hour + minute / 60.0 + second / 3600.0) / 24.0;
    end
end

function r = radians(deg)
    r = deg * pi / 180;
end

function d = degrees(rad)
    d = rad * 180 / pi;
end

function angle = wrapTo360(angle)
    angle = mod(angle, 360);
    indexes = (angle < 0);
    angle(indexes) = angle(indexes) + 360;
end

function angle = wrapTo180(angle)
    angle = wrapTo360(angle + 180);
    angle = angle - 180;
end

function T = calcJulianCent(jd)
    T = (jd.JD - 2451545 + jd.m) / 36525;
end

function L0 = calcGeomMeanLongSun(T)
    L0 = wrapTo360(280.46646 + T * 36000.76983);
end

function M = calcGeomMeanAnomalySun(T)
    M = wrapTo360(357.52911 + T * 35999.05029);
end

function C = calcSunEqOfCenter(T)
    M = calcGeomMeanAnomalySun(T);
    C = sin(radians(M)) .* (1.914602 - 0.004817 * T) + sin(2 * radians(M)) * 0.019993;
end

function R = calcSunRadVector(T)
    M = calcGeomMeanAnomalySun(T);
    R = 1.00014 - 0.01671 * cos(radians(M)) - 0.00014 * cos(2 * radians(M));
end

function eps = calcMeanObliquityOfEcliptic(T)
    eps = 23.4392911 - T * 0.0130042;
end

function [ra, dec] = calcSolarCoordinates(T)
    L0 = calcGeomMeanLongSun(T);
    C = calcSunEqOfCenter(T);
    L = L0 + C - 0.00569;
    eps = calcMeanObliquityOfEcliptic(T);
    ra = degrees(atan2(cos(radians(eps)) .* sin(radians(L)), cos(radians(L))));
    dec = degrees(asin(sin(radians(eps)) .* sin(radians(L))));
end

function GMST = calcGrMeanSiderealTime(jd)
    GMST0 = wrapTo360(100.46061837 + 0.98564736629 * (jd.JD - 2451545));
    GMST = wrapTo360(GMST0 + 360.985647 * jd.m);
end

function [az, el] = equatorial2horizontal(H, dec, lat)
    xhor = cos(radians(H)) .* cos(radians(dec)) .* sin(radians(lat)) - sin(radians(dec)) .* cos(radians(lat));
    yhor = sin(radians(H)) .* cos(radians(dec));
    zhor = cos(radians(H)) .* cos(radians(dec)) .* cos(radians(lat)) + sin(radians(dec)) .* sin(radians(lat));

    az = degrees(atan2(yhor, xhor));
    el = degrees(atan2(zhor, sqrt(xhor.^2 + yhor.^2)));
end

function H = calcHourAngleRiseSet(dec, lat, h0)
    H = degrees(acos((sin(radians(h0)) - sin(radians(lat)) .* sin(radians(dec))) / ...
        (cos(radians(lat)) .* cos(radians(dec)))));
end

function refraction = calcRefraction(el)
    refraction = -20.774 ./ tan(radians(el)) / 3600;
    indexes = (el >= -0.575);
    refraction(indexes) = 1.02 ./ tan(radians(el(indexes) + 10.3 ./ (el(indexes) + 5.11))) / 60;
end

function E = calcEquationOfTime(jd)
    T = calcJulianCent(jd);
    L0 = calcGeomMeanLongSun(T);
    [ra, dec] = calcSolarCoordinates(T);
    E = 4 * wrapTo180(L0 - 0.00569 - ra);
end

function [rt_ascension, declination, radius_vector] = calcEquatorialCoordinates(jd)
    T = calcJulianCent(jd);
    [rt_ascension, declination] = calcSolarCoordinates(T);
    rt_ascension = wrapTo360(rt_ascension);
    radius_vector = calcSunRadVector(T);
end

function [azimuth, elevation] = calcHorizontalCoordinates(jd, latitude, longitude)
    T = calcJulianCent(jd);
    GMST = calcGrMeanSiderealTime(jd);
    [ra, dec] = calcSolarCoordinates(T);
    H = GMST + longitude - ra;
    [azimuth, elevation] = equatorial2horizontal(H, dec, latitude);
    azimuth = azimuth + 180;
    elevation = elevation + calcRefraction(elevation);
end

function [transit, sunrise, sunset] = calcSunriseSunset(jd, latitude, longitude, altitude, iterations)
    m = zeros(1, 3);
    m(1) = 0.5 - longitude / 360;
    for i = 0:iterations
        for event = 1:3
            jd.m = m(event);
            T = calcJulianCent(jd);
            GMST = calcGrMeanSiderealTime(jd);
            [ra, dec] = calcSolarCoordinates(T);
            m0 = jd.m + wrapTo180(ra - longitude - GMST) / 360;
            d0 = calcHourAngleRiseSet(dec, latitude, altitude) / 360;
            if event == 1
                m(1) = m0;
            end
            if event == 2 || i == 0
                m(2) = m0 - d0;
            end
            if event == 3 || i == 0
                m(3) = m0 + d0;
            end
            if i == 0
                break;
            end
        end
    end
    transit = m(1) * 24;
    sunrise = m(2) * 24;
    sunset = m(3) * 24;
end






