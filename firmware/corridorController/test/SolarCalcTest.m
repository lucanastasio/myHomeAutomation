clear all
close all

source("SolarCalculator.m");


mar_eqn = (strptime("2025 20 mar 10:01 CET ", "%Y %d %b %H:%M %Z"));
jun_sol = (strptime("2025 21 jun 04:42 CEST", "%Y %d %b %H:%M %Z"));
sep_eqn = (strptime("2025 22 sep 20:19 CEST", "%Y %d %b %H:%M %Z"));
dec_sol = (strptime("2025 21 dec 16:03 CET ", "%Y %d %b %H:%M %Z"));

times_svec = [(mar_eqn), (jun_sol), (sep_eqn), (dec_sol)];
times_vec = zeros(1, length(times_svec));

for i=1:length(times_svec)
  times_svec(i).hour = 0;
  times_svec(i).min = 0;
  times_vec(i) = mktime(times_svec(i));
end


day_range_1h = (0:1:23) * 3600;
day_range_1m = (0:1:(24*60-1)) * 60;
%day_range_1s = (0:1:(24*60*60-1));

fullday = day_range_1m' + times_vec;

jds = JulianDay(fullday);

POS_LAT = 42.35393125922735;
POS_LON = 13.404711396203966;
POS_ALT = 700;

sun_altitude = 0.0353 * sqrt(POS_ALT);

[azimuth, elevation] = calcHorizontalCoordinates(jds, POS_LAT, POS_LON);
[rt_ascension, declination, radius_vector] = calcEquatorialCoordinates(jds);

am=cos(deg2rad(elevation));#+0.50572*((96.07995-(elevation)).^-1.63641);

am(elevation < 0) = 1;

amn = (am-min(am))./(1-min(am));

amn = amn.^2;

plot(amn);
legend("mar eqn", "jun sol", "sep eqn", "dec sol");


