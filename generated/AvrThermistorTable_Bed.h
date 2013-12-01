/*
 * Copyright (c) 2013 Ambroz Bizjak
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * This file was automatically generated using gen_avr_thermistor_table.py.
 * The following parameters were used for generation:
 * 
 * Name = Bed
 * ResistorR = 4700.0
 * ThermistorR0 = 10000.0
 * ThermistorBeta = 3480.0
 * StartTemp = 20.0
 * EndTemp = 140.0
 * NumBits = 13
 * ScaleFactorExp = 5
 * 
 * The file can be regenerated with the following command:
 * 
 * python gen_avr_thermistor_table.py "Bed" 4700.0 10000.0 3480.0 20.0 140.0 13 5
 */

#ifndef AMBROLIB_AVR_THERMISTOR_Bed_H
#define AMBROLIB_AVR_THERMISTOR_Bed_H

#include <stdint.h>
#ifdef AMBROLIB_AVR
#include <avr/pgmspace.h>
#endif

#include <aprinter/meta/FixedPoint.h>
#include <aprinter/base/Likely.h>

#include <aprinter/BeginNamespace.h>

class AvrThermistorTable_Bed {
public:
    using ValueFixedType = FixedPoint<13, false, -5>;
    
    static ValueFixedType call (uint16_t adc_value)
    {
        if (AMBRO_UNLIKELY(adc_value < 79)) {
            return ValueFixedType::maxValue();
        }
        if (AMBRO_UNLIKELY(adc_value > 740 - 1)) {
            return ValueFixedType::minValue();
        }
        return ValueFixedType::importBits(lookup(adc_value - 79));
    }
    
    static uint16_t invert (ValueFixedType temp_value, bool round_up)
    {
        uint16_t a = 0;
        uint16_t b = 661 - 1;
        if (temp_value > ValueFixedType::importBits(lookup(a))) {
            return 79 + a;
        }
        if (temp_value <= ValueFixedType::importBits(lookup(b))) {
            return 79 + b;
        }
        while (b - a > 1) {
            uint16_t c = a + (b - a) / 2;
            if (temp_value > ValueFixedType::importBits(lookup(c))) {
                b = c;
            } else {
                a = c;
            }
        }
        return 79 + ((temp_value == ValueFixedType::importBits(lookup(a)) || round_up) ? a : b);
    }
    
private:
    inline static uint16_t lookup (uint16_t i)
    {
#ifdef AMBROLIB_AVR
        return pgm_read_word(&table[i]);
#else
        return table[i];
#endif
    }

#ifdef AMBROLIB_AVR
    static uint16_t const table[661] PROGMEM;
#else
    static uint16_t const table[661];
#endif
};

#ifdef AMBROLIB_AVR
uint16_t const AvrThermistorTable_Bed::table[661] PROGMEM = {
#else
uint16_t const AvrThermistorTable_Bed::table[661] = {
#endif
UINT16_C(4460), UINT16_C(4439), 
UINT16_C(4418), UINT16_C(4397), UINT16_C(4377), UINT16_C(4357), UINT16_C(4337), UINT16_C(4318), UINT16_C(4298), UINT16_C(4279), 
UINT16_C(4260), UINT16_C(4242), UINT16_C(4224), UINT16_C(4205), UINT16_C(4188), UINT16_C(4170), UINT16_C(4152), UINT16_C(4135), 
UINT16_C(4118), UINT16_C(4101), UINT16_C(4085), UINT16_C(4068), UINT16_C(4052), UINT16_C(4036), UINT16_C(4020), UINT16_C(4005), 
UINT16_C(3989), UINT16_C(3974), UINT16_C(3958), UINT16_C(3943), UINT16_C(3929), UINT16_C(3914), UINT16_C(3899), UINT16_C(3885), 
UINT16_C(3871), UINT16_C(3856), UINT16_C(3842), UINT16_C(3829), UINT16_C(3815), UINT16_C(3801), UINT16_C(3788), UINT16_C(3774), 
UINT16_C(3761), UINT16_C(3748), UINT16_C(3735), UINT16_C(3722), UINT16_C(3710), UINT16_C(3697), UINT16_C(3684), UINT16_C(3672), 
UINT16_C(3660), UINT16_C(3647), UINT16_C(3635), UINT16_C(3623), UINT16_C(3612), UINT16_C(3600), UINT16_C(3588), UINT16_C(3576), 
UINT16_C(3565), UINT16_C(3553), UINT16_C(3542), UINT16_C(3531), UINT16_C(3520), UINT16_C(3509), UINT16_C(3498), UINT16_C(3487), 
UINT16_C(3476), UINT16_C(3465), UINT16_C(3455), UINT16_C(3444), UINT16_C(3434), UINT16_C(3423), UINT16_C(3413), UINT16_C(3403), 
UINT16_C(3392), UINT16_C(3382), UINT16_C(3372), UINT16_C(3362), UINT16_C(3352), UINT16_C(3342), UINT16_C(3333), UINT16_C(3323), 
UINT16_C(3313), UINT16_C(3304), UINT16_C(3294), UINT16_C(3285), UINT16_C(3275), UINT16_C(3266), UINT16_C(3257), UINT16_C(3248), 
UINT16_C(3238), UINT16_C(3229), UINT16_C(3220), UINT16_C(3211), UINT16_C(3202), UINT16_C(3193), UINT16_C(3185), UINT16_C(3176), 
UINT16_C(3167), UINT16_C(3158), UINT16_C(3150), UINT16_C(3141), UINT16_C(3133), UINT16_C(3124), UINT16_C(3116), UINT16_C(3107), 
UINT16_C(3099), UINT16_C(3091), UINT16_C(3083), UINT16_C(3074), UINT16_C(3066), UINT16_C(3058), UINT16_C(3050), UINT16_C(3042), 
UINT16_C(3034), UINT16_C(3026), UINT16_C(3018), UINT16_C(3010), UINT16_C(3003), UINT16_C(2995), UINT16_C(2987), UINT16_C(2979), 
UINT16_C(2972), UINT16_C(2964), UINT16_C(2957), UINT16_C(2949), UINT16_C(2942), UINT16_C(2934), UINT16_C(2927), UINT16_C(2919), 
UINT16_C(2912), UINT16_C(2905), UINT16_C(2897), UINT16_C(2890), UINT16_C(2883), UINT16_C(2876), UINT16_C(2869), UINT16_C(2862), 
UINT16_C(2854), UINT16_C(2847), UINT16_C(2840), UINT16_C(2833), UINT16_C(2826), UINT16_C(2820), UINT16_C(2813), UINT16_C(2806), 
UINT16_C(2799), UINT16_C(2792), UINT16_C(2785), UINT16_C(2779), UINT16_C(2772), UINT16_C(2765), UINT16_C(2759), UINT16_C(2752), 
UINT16_C(2745), UINT16_C(2739), UINT16_C(2732), UINT16_C(2726), UINT16_C(2719), UINT16_C(2713), UINT16_C(2706), UINT16_C(2700), 
UINT16_C(2694), UINT16_C(2687), UINT16_C(2681), UINT16_C(2675), UINT16_C(2668), UINT16_C(2662), UINT16_C(2656), UINT16_C(2650), 
UINT16_C(2643), UINT16_C(2637), UINT16_C(2631), UINT16_C(2625), UINT16_C(2619), UINT16_C(2613), UINT16_C(2607), UINT16_C(2601), 
UINT16_C(2595), UINT16_C(2589), UINT16_C(2583), UINT16_C(2577), UINT16_C(2571), UINT16_C(2565), UINT16_C(2559), UINT16_C(2553), 
UINT16_C(2547), UINT16_C(2542), UINT16_C(2536), UINT16_C(2530), UINT16_C(2524), UINT16_C(2519), UINT16_C(2513), UINT16_C(2507), 
UINT16_C(2501), UINT16_C(2496), UINT16_C(2490), UINT16_C(2485), UINT16_C(2479), UINT16_C(2473), UINT16_C(2468), UINT16_C(2462), 
UINT16_C(2457), UINT16_C(2451), UINT16_C(2446), UINT16_C(2440), UINT16_C(2435), UINT16_C(2429), UINT16_C(2424), UINT16_C(2418), 
UINT16_C(2413), UINT16_C(2408), UINT16_C(2402), UINT16_C(2397), UINT16_C(2392), UINT16_C(2386), UINT16_C(2381), UINT16_C(2376), 
UINT16_C(2371), UINT16_C(2365), UINT16_C(2360), UINT16_C(2355), UINT16_C(2350), UINT16_C(2344), UINT16_C(2339), UINT16_C(2334), 
UINT16_C(2329), UINT16_C(2324), UINT16_C(2319), UINT16_C(2314), UINT16_C(2308), UINT16_C(2303), UINT16_C(2298), UINT16_C(2293), 
UINT16_C(2288), UINT16_C(2283), UINT16_C(2278), UINT16_C(2273), UINT16_C(2268), UINT16_C(2263), UINT16_C(2258), UINT16_C(2253), 
UINT16_C(2248), UINT16_C(2244), UINT16_C(2239), UINT16_C(2234), UINT16_C(2229), UINT16_C(2224), UINT16_C(2219), UINT16_C(2214), 
UINT16_C(2210), UINT16_C(2205), UINT16_C(2200), UINT16_C(2195), UINT16_C(2190), UINT16_C(2186), UINT16_C(2181), UINT16_C(2176), 
UINT16_C(2171), UINT16_C(2167), UINT16_C(2162), UINT16_C(2157), UINT16_C(2152), UINT16_C(2148), UINT16_C(2143), UINT16_C(2138), 
UINT16_C(2134), UINT16_C(2129), UINT16_C(2125), UINT16_C(2120), UINT16_C(2115), UINT16_C(2111), UINT16_C(2106), UINT16_C(2102), 
UINT16_C(2097), UINT16_C(2092), UINT16_C(2088), UINT16_C(2083), UINT16_C(2079), UINT16_C(2074), UINT16_C(2070), UINT16_C(2065), 
UINT16_C(2061), UINT16_C(2056), UINT16_C(2052), UINT16_C(2047), UINT16_C(2043), UINT16_C(2039), UINT16_C(2034), UINT16_C(2030), 
UINT16_C(2025), UINT16_C(2021), UINT16_C(2016), UINT16_C(2012), UINT16_C(2008), UINT16_C(2003), UINT16_C(1999), UINT16_C(1995), 
UINT16_C(1990), UINT16_C(1986), UINT16_C(1982), UINT16_C(1977), UINT16_C(1973), UINT16_C(1969), UINT16_C(1964), UINT16_C(1960), 
UINT16_C(1956), UINT16_C(1952), UINT16_C(1947), UINT16_C(1943), UINT16_C(1939), UINT16_C(1935), UINT16_C(1930), UINT16_C(1926), 
UINT16_C(1922), UINT16_C(1918), UINT16_C(1913), UINT16_C(1909), UINT16_C(1905), UINT16_C(1901), UINT16_C(1897), UINT16_C(1892), 
UINT16_C(1888), UINT16_C(1884), UINT16_C(1880), UINT16_C(1876), UINT16_C(1872), UINT16_C(1868), UINT16_C(1864), UINT16_C(1859), 
UINT16_C(1855), UINT16_C(1851), UINT16_C(1847), UINT16_C(1843), UINT16_C(1839), UINT16_C(1835), UINT16_C(1831), UINT16_C(1827), 
UINT16_C(1823), UINT16_C(1819), UINT16_C(1815), UINT16_C(1811), UINT16_C(1806), UINT16_C(1802), UINT16_C(1798), UINT16_C(1794), 
UINT16_C(1790), UINT16_C(1786), UINT16_C(1782), UINT16_C(1778), UINT16_C(1774), UINT16_C(1770), UINT16_C(1766), UINT16_C(1763), 
UINT16_C(1759), UINT16_C(1755), UINT16_C(1751), UINT16_C(1747), UINT16_C(1743), UINT16_C(1739), UINT16_C(1735), UINT16_C(1731), 
UINT16_C(1727), UINT16_C(1723), UINT16_C(1719), UINT16_C(1715), UINT16_C(1711), UINT16_C(1707), UINT16_C(1704), UINT16_C(1700), 
UINT16_C(1696), UINT16_C(1692), UINT16_C(1688), UINT16_C(1684), UINT16_C(1680), UINT16_C(1676), UINT16_C(1673), UINT16_C(1669), 
UINT16_C(1665), UINT16_C(1661), UINT16_C(1657), UINT16_C(1653), UINT16_C(1650), UINT16_C(1646), UINT16_C(1642), UINT16_C(1638), 
UINT16_C(1634), UINT16_C(1631), UINT16_C(1627), UINT16_C(1623), UINT16_C(1619), UINT16_C(1615), UINT16_C(1612), UINT16_C(1608), 
UINT16_C(1604), UINT16_C(1600), UINT16_C(1596), UINT16_C(1593), UINT16_C(1589), UINT16_C(1585), UINT16_C(1581), UINT16_C(1578), 
UINT16_C(1574), UINT16_C(1570), UINT16_C(1566), UINT16_C(1563), UINT16_C(1559), UINT16_C(1555), UINT16_C(1551), UINT16_C(1548), 
UINT16_C(1544), UINT16_C(1540), UINT16_C(1537), UINT16_C(1533), UINT16_C(1529), UINT16_C(1525), UINT16_C(1522), UINT16_C(1518), 
UINT16_C(1514), UINT16_C(1511), UINT16_C(1507), UINT16_C(1503), UINT16_C(1500), UINT16_C(1496), UINT16_C(1492), UINT16_C(1489), 
UINT16_C(1485), UINT16_C(1481), UINT16_C(1478), UINT16_C(1474), UINT16_C(1470), UINT16_C(1467), UINT16_C(1463), UINT16_C(1459), 
UINT16_C(1456), UINT16_C(1452), UINT16_C(1448), UINT16_C(1445), UINT16_C(1441), UINT16_C(1437), UINT16_C(1434), UINT16_C(1430), 
UINT16_C(1427), UINT16_C(1423), UINT16_C(1419), UINT16_C(1416), UINT16_C(1412), UINT16_C(1408), UINT16_C(1405), UINT16_C(1401), 
UINT16_C(1398), UINT16_C(1394), UINT16_C(1390), UINT16_C(1387), UINT16_C(1383), UINT16_C(1380), UINT16_C(1376), UINT16_C(1372), 
UINT16_C(1369), UINT16_C(1365), UINT16_C(1362), UINT16_C(1358), UINT16_C(1354), UINT16_C(1351), UINT16_C(1347), UINT16_C(1344), 
UINT16_C(1340), UINT16_C(1337), UINT16_C(1333), UINT16_C(1329), UINT16_C(1326), UINT16_C(1322), UINT16_C(1319), UINT16_C(1315), 
UINT16_C(1312), UINT16_C(1308), UINT16_C(1304), UINT16_C(1301), UINT16_C(1297), UINT16_C(1294), UINT16_C(1290), UINT16_C(1287), 
UINT16_C(1283), UINT16_C(1279), UINT16_C(1276), UINT16_C(1272), UINT16_C(1269), UINT16_C(1265), UINT16_C(1262), UINT16_C(1258), 
UINT16_C(1255), UINT16_C(1251), UINT16_C(1248), UINT16_C(1244), UINT16_C(1240), UINT16_C(1237), UINT16_C(1233), UINT16_C(1230), 
UINT16_C(1226), UINT16_C(1223), UINT16_C(1219), UINT16_C(1216), UINT16_C(1212), UINT16_C(1209), UINT16_C(1205), UINT16_C(1201), 
UINT16_C(1198), UINT16_C(1194), UINT16_C(1191), UINT16_C(1187), UINT16_C(1184), UINT16_C(1180), UINT16_C(1177), UINT16_C(1173), 
UINT16_C(1170), UINT16_C(1166), UINT16_C(1163), UINT16_C(1159), UINT16_C(1156), UINT16_C(1152), UINT16_C(1148), UINT16_C(1145), 
UINT16_C(1141), UINT16_C(1138), UINT16_C(1134), UINT16_C(1131), UINT16_C(1127), UINT16_C(1124), UINT16_C(1120), UINT16_C(1117), 
UINT16_C(1113), UINT16_C(1110), UINT16_C(1106), UINT16_C(1103), UINT16_C(1099), UINT16_C(1095), UINT16_C(1092), UINT16_C(1088), 
UINT16_C(1085), UINT16_C(1081), UINT16_C(1078), UINT16_C(1074), UINT16_C(1071), UINT16_C(1067), UINT16_C(1064), UINT16_C(1060), 
UINT16_C(1057), UINT16_C(1053), UINT16_C(1049), UINT16_C(1046), UINT16_C(1042), UINT16_C(1039), UINT16_C(1035), UINT16_C(1032), 
UINT16_C(1028), UINT16_C(1025), UINT16_C(1021), UINT16_C(1018), UINT16_C(1014), UINT16_C(1010), UINT16_C(1007), UINT16_C(1003), 
UINT16_C(1000), UINT16_C(996), UINT16_C(993), UINT16_C(989), UINT16_C(986), UINT16_C(982), UINT16_C(978), UINT16_C(975), 
UINT16_C(971), UINT16_C(968), UINT16_C(964), UINT16_C(961), UINT16_C(957), UINT16_C(954), UINT16_C(950), UINT16_C(946), 
UINT16_C(943), UINT16_C(939), UINT16_C(936), UINT16_C(932), UINT16_C(928), UINT16_C(925), UINT16_C(921), UINT16_C(918), 
UINT16_C(914), UINT16_C(911), UINT16_C(907), UINT16_C(903), UINT16_C(900), UINT16_C(896), UINT16_C(893), UINT16_C(889), 
UINT16_C(885), UINT16_C(882), UINT16_C(878), UINT16_C(874), UINT16_C(871), UINT16_C(867), UINT16_C(864), UINT16_C(860), 
UINT16_C(856), UINT16_C(853), UINT16_C(849), UINT16_C(846), UINT16_C(842), UINT16_C(838), UINT16_C(835), UINT16_C(831), 
UINT16_C(827), UINT16_C(824), UINT16_C(820), UINT16_C(816), UINT16_C(813), UINT16_C(809), UINT16_C(805), UINT16_C(802), 
UINT16_C(798), UINT16_C(794), UINT16_C(791), UINT16_C(787), UINT16_C(783), UINT16_C(780), UINT16_C(776), UINT16_C(772), 
UINT16_C(769), UINT16_C(765), UINT16_C(761), UINT16_C(757), UINT16_C(754), UINT16_C(750), UINT16_C(746), UINT16_C(743), 
UINT16_C(739), UINT16_C(735), UINT16_C(731), UINT16_C(728), UINT16_C(724), UINT16_C(720), UINT16_C(716), UINT16_C(713), 
UINT16_C(709), UINT16_C(705), UINT16_C(701), UINT16_C(698), UINT16_C(694), UINT16_C(690), UINT16_C(686), UINT16_C(682), 
UINT16_C(679), UINT16_C(675), UINT16_C(671), UINT16_C(667), UINT16_C(663), UINT16_C(660), UINT16_C(656), UINT16_C(652), 
UINT16_C(648), UINT16_C(644), UINT16_C(641), 
};

#include <aprinter/EndNamespace.h>

#endif 
