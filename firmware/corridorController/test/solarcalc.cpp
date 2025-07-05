#include <math.h>
struct JulianDay{
    double JD;
    double m;
    explicit JulianDay(unsigned long utc);
    JulianDay(int year, int month, int day, int hour = 0, int minute = 0, int second = 0);
};
JulianDay::JulianDay(unsigned long utc){
    JD = static_cast<unsigned long>(utc / 86400) + 2440587.5;
    m = (utc % 86400) / 86400.0;
}
JulianDay::JulianDay(int Y, int M, int D, int hour, int minute, int second){
    JD = 367.0 * Y - static_cast<int>(7 * (Y + (M + 9) / 12) / 4) + static_cast<int>(275 * M / 9) + D + 1721013.5;
    m = (hour + minute / 60.0 + second / 3600.0) / 24.0;
}
double radians(double deg){
    return deg * M_PI / 180;
}
double degrees(double rad){
    return rad * 180 / M_PI;
}
double wrapTo360(double angle){
    angle = fmod(angle, 360);
    if (angle < 0) angle += 360;
    return angle;
}
double wrapTo180(double angle){
    angle = wrapTo360(angle + 180);
    return angle - 180;
}
double calcJulianCent(JulianDay jd){
    return (jd.JD - 2451545 + jd.m) / 36525;
}
double calcGeomMeanLongSun(double T){
    return wrapTo360(280.46646 + T * 36000.76983);
}
double calcGeomMeanAnomalySun(double T){
    return wrapTo360(357.52911 + T * 35999.05029);
}
double calcSunEqOfCenter(double T){
    double M = calcGeomMeanAnomalySun(T);
    return sin(radians(M)) * (1.914602 - 0.004817 * T) + sin(2 * radians(M)) * 0.019993;
}
double calcSunRadVector(double T){
    double M = calcGeomMeanAnomalySun(T);
    return 1.00014 - 0.01671 * cos(radians(M)) - 0.00014 * cos(2 * radians(M));  
}
double calcMeanObliquityOfEcliptic(double T){
    return 23.4392911 - T * 0.0130042;
}
void calcSolarCoordinates(double T, double& ra, double& dec){
    double L0 = calcGeomMeanLongSun(T);
    double C = calcSunEqOfCenter(T);
    double L = L0 + C - 0.00569; 
    double eps = calcMeanObliquityOfEcliptic(T);
    ra = degrees(atan2(cos(radians(eps)) * sin(radians(L)), cos(radians(L)))); 
    dec = degrees(asin(sin(radians(eps)) * sin(radians(L))));
}
double calcGrMeanSiderealTime(JulianDay jd){
    double GMST0 = wrapTo360(100.46061837 + 0.98564736629 * (jd.JD - 2451545));
    return wrapTo360(GMST0 + 360.985647 * jd.m);
}
void equatorial2horizontal(double H, double dec, double lat, double& az, double& el){
    double xhor = cos(radians(H)) * cos(radians(dec)) * sin(radians(lat)) - sin(radians(dec)) * cos(radians(lat));
    double yhor = sin(radians(H)) * cos(radians(dec));
    double zhor = cos(radians(H)) * cos(radians(dec)) * cos(radians(lat)) + sin(radians(dec)) * sin(radians(lat));

    az = degrees(atan2(yhor, xhor));
    el = degrees(atan2(zhor, sqrt(xhor * xhor + yhor * yhor)));
}
double calcHourAngleRiseSet(double dec, double lat, double h0){
    return degrees(acos((sin(radians(h0)) - sin(radians(lat)) * sin(radians(dec))) /
                   (cos(radians(lat)) * cos(radians(dec)))));
}
double calcRefraction(double el){
    if (el < -0.575)
        return -20.774 / tan(radians(el)) / 3600;  
    else
        return 1.02 / tan(radians(el + 10.3 / (el + 5.11))) / 60;  
}
void calcEquationOfTime(JulianDay jd, double& E){
    double T = calcJulianCent(jd);
    double L0 = calcGeomMeanLongSun(T);
    double ra, dec;
    calcSolarCoordinates(T, ra, dec);
    E = 4 * wrapTo180(L0 - 0.00569 - ra);
}
void calcEquatorialCoordinates(JulianDay jd, double& rt_ascension, double& declination, double& radius_vector){
    double T = calcJulianCent(jd);
    calcSolarCoordinates(T, rt_ascension, declination);
    rt_ascension = wrapTo360(rt_ascension);
    radius_vector = calcSunRadVector(T);
}
void calcHorizontalCoordinates(JulianDay jd, double latitude, double longitude, double& azimuth, double& elevation){
    double T = calcJulianCent(jd);
    double GMST = calcGrMeanSiderealTime(jd);
    double ra, dec;
    calcSolarCoordinates(T, ra, dec);
    double H = GMST + longitude - ra;
    equatorial2horizontal(H, dec, latitude, azimuth, elevation);
    azimuth += 180; 
    elevation += calcRefraction(elevation);
}
void calcSunriseSunset(JulianDay jd, double latitude, double longitude, double& transit, double& sunrise, double& sunset, double altitude, int iterations){
    double m[3];
    m[0] = 0.5 - longitude / 360;
    for (int i = 0; i <= iterations; ++i)
        for (int event = 0; event < 3; ++event)
        {
            jd.m = m[event];
            double T = calcJulianCent(jd);
            double GMST = calcGrMeanSiderealTime(jd);
            double ra, dec;
            calcSolarCoordinates(T, ra, dec);
            double m0 = jd.m + wrapTo180(ra - longitude - GMST) / 360;
            double d0 = calcHourAngleRiseSet(dec, latitude, altitude) / 360;
            if (event == 0) m[0] = m0;
            if (event == 1 || i == 0) m[1] = m0 - d0;
            if (event == 2 || i == 0) m[2] = m0 + d0;
            if (i == 0) break;
        }
    transit = m[0] * 24;
    sunrise = m[1] * 24;
    sunset = m[2] * 24;
}
