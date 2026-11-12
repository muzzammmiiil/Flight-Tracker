// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <ctype.h>

// #define AIRPORT_FILE  "airports.txt"
// #define AIRCRAFT_FILE "aircraft.txt"
// #define FLIGHT_FILE   "flights.txt"

// #define LOCAL_UTC     5.0f   /* "local" time zone of the user (Pakistan, UTC+5) */
// #define DAY           1440   /* minutes in a day                                */
// #define GROUND_WINDOW 60     /* minutes a plane is shown "on ground" after landing
//                                 or before departure                              */
// #define DEFAULT_SPEED 850

// /* ------------------------------------------------------------------ */
// /*  Data types                                                        */
// /* ------------------------------------------------------------------ */
// typedef struct {
//     char  code[4];
//     char  name[40];
//     char  city[30];
//     float utc;                 /* offset from UTC in hours */
// } Airport;

// typedef struct {
//     char reg[12];
//     char model[24];
//     char airline[30];
//     int  speed;                /* cruise speed km/h */
// } Aircraft;

// typedef struct {
//     int  id;
//     char number[10];
//     char airline[30];
//     char reg[12];
//     char origin[4];
//     char dest[4];
//     int  depMin;               /* scheduled departure, minutes since 00:00 UTC */
//     int  durMin;               /* flight duration in minutes                   */
//     int  delayMin;
//     int  cancelled;
//     int  distKm;
//     int  cruiseAlt;            /* feet */
// } Flight;

// typedef enum {
//     ST_SCHEDULED, ST_DELAYED, ST_AIRBORNE, ST_LANDED, ST_CANCELLED
// } Status;

// static const char *STATUS_TEXT[] = {
//     "SCHEDULED", "DELAYED", "AIRBORNE", "LANDED", "CANCELLED"
// };

// /* ------------------------------------------------------------------ */
// /*  Global database (dynamic arrays)                                  */
// /* ------------------------------------------------------------------ */
// static Airport  *gAirports;  static int gAirportCount, gAirportCap;
// static Aircraft *gAircraft;  static int gAircraftCount, gAircraftCap;
// static Flight   *gFlights;   static int gFlightCount,  gFlightCap;

// static int gClock   = 600;   /* simulated time, minutes since 00:00 UTC (10:00) */
// static int gEof     = 0;     /* set when stdin ends                             */
// static int gSortKey = 1;     /* used by the qsort comparator                    */

// /* ------------------------------------------------------------------ */
// /*  Small helpers                                                     */
// /* ------------------------------------------------------------------ */
// static void *grow(void *p, int *cap, size_t size)
// {
//     int   n = (*cap == 0) ? 8 : *cap * 2;
//     void *q = realloc(p, (size_t)n * size);
//     if (!q) { printf("Out of memory!\n"); exit(1); }
//     *cap = n;
//     return q;
// }

// static void toUpper(char *s)
// {
//     for (; *s; s++) *s = (char)toupper((unsigned char)*s);
// }

// static int containsIC(const char *hay, const char *needle)
// {
//     char a[128], b[128];
//     strncpy(a, hay, sizeof a - 1);    a[sizeof a - 1] = '\0';
//     strncpy(b, needle, sizeof b - 1); b[sizeof b - 1] = '\0';
//     toUpper(a); toUpper(b);
//     return strstr(a, b) != NULL;
// }

// static int equalIC(const char *x, const char *y)
// {
//     char a[128], b[128];
//     strncpy(a, x, sizeof a - 1); a[sizeof a - 1] = '\0';
//     strncpy(b, y, sizeof b - 1); b[sizeof b - 1] = '\0';
//     toUpper(a); toUpper(b);
//     return strcmp(a, b) == 0;
// }

// static int isBlankLine(const char *s)
// {
//     while (*s == ' ' || *s == '\t') s++;
//     return *s == '\0' || *s == '\n' || *s == '\r' || *s == '#';
// }

// static void readLine(const char *prompt, char *buf, size_t n)
// {
//     printf("%s", prompt);
//     fflush(stdout);
//     if (!fgets(buf, (int)n, stdin)) { buf[0] = '\0'; gEof = 1; return; }
//     buf[strcspn(buf, "\r\n")] = '\0';
// }

// /* reads an integer in [lo, hi]; returns 1 on success, 0 on invalid input */
// static int readIntRange(const char *prompt, int lo, int hi, int *out)
// {
//     char  b[64], *end;
//     long  v;
//     readLine(prompt, b, sizeof b);
//     v = strtol(b, &end, 10);
//     if (end == b || *end != '\0' || v < lo || v > hi) {
//         printf("  [!] Invalid input (expected %d to %d).\n", lo, hi);
//         return 0;
//     }
//     *out = (int)v;
//     return 1;
// }

// /* parses "HH:MM" -> minutes, returns 1 on success */
// static int parseHHMM(const char *s, int *minutes)
// {
//     int h, m;
//     char extra;
//     if (sscanf(s, " %d:%d %c", &h, &m, &extra) != 2) return 0;
//     if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
//     *minutes = h * 60 + m;
//     return 1;
// }

// static void pressEnter(void)
// {
//     char tmp[8];
//     readLine("\nPress Enter to continue...", tmp, sizeof tmp);
// }

// /* ------------------------------------------------------------------ */
// /*  Time formatting                                                   */
// /* ------------------------------------------------------------------ */
// static void fmtUtc(char *buf, int minutes)
// {
//     int d = minutes / DAY, m = minutes % DAY;
//     if (d > 0) sprintf(buf, "%02d:%02d+%d", m / 60, m % 60, d);
//     else       sprintf(buf, "%02d:%02d", m / 60, m % 60);
// }

// static void fmtLocal(char *buf, int utcMinutes, const Airport *ap)
// {
//     int total = utcMinutes + (int)(ap->utc * 60 + (ap->utc >= 0 ? 0.5f : -0.5f));
//     int dd = 0;
//     if (total < 0)          { dd = -1; total += DAY; }
//     else if (total >= DAY)  { dd = total / DAY; total %= DAY; }
//     if (dd > 0)      sprintf(buf, "%02d:%02d+%d", total / 60, total % 60, dd);
//     else if (dd < 0) sprintf(buf, "%02d:%02d-1", total / 60, total % 60);
//     else             sprintf(buf, "%02d:%02d", total / 60, total % 60);
// }

// /* ------------------------------------------------------------------ */
// /*  Lookups                                                           */
// /* ------------------------------------------------------------------ */
// static Airport *findAirport(const char *code)
// {
//     int i;
//     for (i = 0; i < gAirportCount; i++)
//         if (equalIC(gAirports[i].code, code)) return &gAirports[i];
//     return NULL;
// }

// static Aircraft *findAircraft(const char *reg)
// {
//     int i;
//     for (i = 0; i < gAircraftCount; i++)
//         if (equalIC(gAircraft[i].reg, reg)) return &gAircraft[i];
//     return NULL;
// }

// static int idExists(int id)
// {
//     int i;
//     for (i = 0; i < gFlightCount; i++)
//         if (gFlights[i].id == id) return 1;
//     return 0;
// }

// /* RECURSIVE binary search on gFlights (kept sorted by id) */
// static int binarySearchId(int lo, int hi, int id)
// {
//     int mid;
//     if (lo > hi) return -1;
//     mid = lo + (hi - lo) / 2;
//     if (gFlights[mid].id == id) return mid;
//     if (gFlights[mid].id < id)  return binarySearchId(mid + 1, hi, id);
//     return binarySearchId(lo, mid - 1, id);
// }

// static int cmpFlightId(const void *a, const void *b)
// {
//     return ((const Flight *)a)->id - ((const Flight *)b)->id;
// }

// static void sortFlightsById(void)
// {
//     qsort(gFlights, (size_t)gFlightCount, sizeof(Flight), cmpFlightId);
// }

// /* ------------------------------------------------------------------ */
// /*  Simulation logic                                                  */
// /* ------------------------------------------------------------------ */
// static int actualDep(const Flight *f) { return f->depMin + f->delayMin; }
// static int actualArr(const Flight *f) { return actualDep(f) + f->durMin; }

// static Status getStatus(const Flight *f, int now)
// {
//     if (f->cancelled)            return ST_CANCELLED;
//     if (now < actualDep(f))      return f->delayMin > 0 ? ST_DELAYED : ST_SCHEDULED;
//     if (now < actualArr(f))      return ST_AIRBORNE;
//     return ST_LANDED;
// }

// static double flightProgress(const Flight *f, int now)
// {
//     double p = (double)(now - actualDep(f)) / (double)f->durMin;
//     if (p < 0) p = 0;
//     if (p > 1) p = 1;
//     return p;
// }

// static int cruiseSpeedOf(const Flight *f)
// {
//     Aircraft *a = findAircraft(f->reg);
//     return a ? a->speed : DEFAULT_SPEED;
// }

// static int currentAltitude(const Flight *f, int now)
// {
//     double p = flightProgress(f, now);
//     if (p < 0.15) return (int)(f->cruiseAlt * p / 0.15);          /* climb   */
//     if (p > 0.85) return (int)(f->cruiseAlt * (1.0 - p) / 0.15);  /* descent */
//     return f->cruiseAlt;
// }

// static int currentSpeed(const Flight *f, int now)
// {
//     double r = (double)currentAltitude(f, now) / (double)f->cruiseAlt;
//     return (int)(cruiseSpeedOf(f) * (0.6 + 0.4 * r));
// }

// /* ETA = now + remaining distance / speed (airborne), else scheduled arrival */
// static int calcEta(const Flight *f, int now)
// {
//     if (getStatus(f, now) == ST_AIRBORNE) {
//         double remaining = f->distKm * (1.0 - flightProgress(f, now));
//         return now + (int)(remaining / cruiseSpeedOf(f) * 60.0 + 0.5);
//     }
//     return actualArr(f);
// }

// static void statusLabel(const Flight *f, int now, char *buf)
// {
//     Status st = getStatus(f, now);
//     if (st == ST_DELAYED) sprintf(buf, "DELAYED +%dm", f->delayMin);
//     else                  strcpy(buf, STATUS_TEXT[st]);
// }

// /* ------------------------------------------------------------------ */
// /*  File handling                                                     */
// /* ------------------------------------------------------------------ */
// static int loadAirports(void)
// {
//     FILE *fp = fopen(AIRPORT_FILE, "r");
//     char line[256];
//     int  lineNo = 0;
//     if (!fp) { printf("  [!] Cannot open %s\n", AIRPORT_FILE); return 0; }
//     while (fgets(line, sizeof line, fp)) {
//         Airport a;
//         lineNo++;
//         if (isBlankLine(line)) continue;
//         if (sscanf(line, " %3[^|]|%39[^|]|%29[^|]|%f",
//                    a.code, a.name, a.city, &a.utc) != 4) {
//             printf("  [!] %s line %d skipped (bad format)\n", AIRPORT_FILE, lineNo);
//             continue;
//         }
//         toUpper(a.code);
//         if (findAirport(a.code)) {
//             printf("  [!] %s line %d skipped (duplicate code %s)\n", AIRPORT_FILE, lineNo, a.code);
//             continue;
//         }
//         if (gAirportCount == gAirportCap)
//             gAirports = grow(gAirports, &gAirportCap, sizeof(Airport));
//         gAirports[gAirportCount++] = a;
//     }
//     fclose(fp);
//     return 1;
// }

// static int loadAircraft(void)
// {
//     FILE *fp = fopen(AIRCRAFT_FILE, "r");
//     char line[256];
//     int  lineNo = 0;
//     if (!fp) { printf("  [!] Cannot open %s\n", AIRCRAFT_FILE); return 0; }
//     while (fgets(line, sizeof line, fp)) {
//         Aircraft a;
//         lineNo++;
//         if (isBlankLine(line)) continue;
//         if (sscanf(line, " %11[^|]|%23[^|]|%29[^|]|%d",
//                    a.reg, a.model, a.airline, &a.speed) != 4 || a.speed <= 0) {
//             printf("  [!] %s line %d skipped (bad format)\n", AIRCRAFT_FILE, lineNo);
//             continue;
//         }
//         toUpper(a.reg);
//         if (findAircraft(a.reg)) {
//             printf("  [!] %s line %d skipped (duplicate reg %s)\n", AIRCRAFT_FILE, lineNo, a.reg);
//             continue;
//         }
//         if (gAircraftCount == gAircraftCap)
//             gAircraft = grow(gAircraft, &gAircraftCap, sizeof(Aircraft));
//         gAircraft[gAircraftCount++] = a;
//     }
//     fclose(fp);
//     return 1;
// }

// static int loadFlights(void)
// {
//     FILE *fp = fopen(FLIGHT_FILE, "r");
//     char line[256];
//     int  lineNo = 0;
//     if (!fp) { printf("  [!] Cannot open %s\n", FLIGHT_FILE); return 0; }
//     while (fgets(line, sizeof line, fp)) {
//         Flight f;
//         int h, m;
//         lineNo++;
//         if (isBlankLine(line)) continue;
//         if (sscanf(line, " %d|%9[^|]|%29[^|]|%11[^|]|%3[^|]|%3[^|]|%d:%d|%d|%d|%d|%d|%d",
//                    &f.id, f.number, f.airline, f.reg, f.origin, f.dest,
//                    &h, &m, &f.durMin, &f.delayMin, &f.cancelled,
//                    &f.distKm, &f.cruiseAlt) != 13) {
//             printf("  [!] %s line %d skipped (bad format)\n", FLIGHT_FILE, lineNo);
//             continue;
//         }
//         toUpper(f.origin); toUpper(f.dest); toUpper(f.reg);
//         f.depMin = h * 60 + m;
//         if (f.id <= 0 || h < 0 || h > 23 || m < 0 || m > 59 || f.durMin <= 0 ||
//             f.delayMin < 0 || (f.cancelled != 0 && f.cancelled != 1) ||
//             f.distKm <= 0 || f.cruiseAlt <= 0 || strcmp(f.origin, f.dest) == 0) {
//             printf("  [!] %s line %d skipped (invalid values)\n", FLIGHT_FILE, lineNo);
//             continue;
//         }
//         if (idExists(f.id)) {
//             printf("  [!] %s line %d skipped (duplicate flight ID %d)\n", FLIGHT_FILE, lineNo, f.id);
//             continue;
//         }
//         if (!findAirport(f.origin) || !findAirport(f.dest)) {
//             printf("  [!] %s line %d skipped (unknown airport)\n", FLIGHT_FILE, lineNo);
//             continue;
//         }
//         if (!findAircraft(f.reg)) {
//             printf("  [!] %s line %d skipped (unknown aircraft %s)\n", FLIGHT_FILE, lineNo, f.reg);
//             continue;
//         }
//         if (gFlightCount == gFlightCap)
//             gFlights = grow(gFlights, &gFlightCap, sizeof(Flight));
//         gFlights[gFlightCount++] = f;
//     }
//     fclose(fp);
//     sortFlightsById();
//     return 1;
// }

// static int saveFlights(void)
// {
//     FILE *fp = fopen(FLIGHT_FILE, "w");
//     int i;
//     if (!fp) { printf("  [!] Cannot write %s\n", FLIGHT_FILE); return 0; }
//     fprintf(fp, "# id|flight no|airline|aircraft reg|origin|dest|dep UTC HH:MM|"
//                 "duration min|delay min|cancelled(0/1)|distance km|cruise alt ft\n");
//     for (i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         fprintf(fp, "%d|%s|%s|%s|%s|%s|%02d:%02d|%d|%d|%d|%d|%d\n",
//                 f->id, f->number, f->airline, f->reg, f->origin, f->dest,
//                 f->depMin / 60, f->depMin % 60, f->durMin, f->delayMin,
//                 f->cancelled, f->distKm, f->cruiseAlt);
//     }
//     fclose(fp);
//     return 1;
// }

// static void loadAll(void)
// {
//     free(gAirports); free(gAircraft); free(gFlights);
//     gAirports = NULL; gAircraft = NULL; gFlights = NULL;
//     gAirportCount = gAirportCap = 0;
//     gAircraftCount = gAircraftCap = 0;
//     gFlightCount = gFlightCap = 0;

//     loadAirports();
//     loadAircraft();
//     loadFlights();
//     printf("  Loaded: %d airports, %d aircraft, %d flights.\n",
//            gAirportCount, gAircraftCount, gFlightCount);
// }

// /* ------------------------------------------------------------------ */
// /*  Flight tables / details                                           */
// /* ------------------------------------------------------------------ */
// static Flight **collectFlights(int *n)
// {
//     int i;
//     Flight **list = malloc(sizeof(Flight *) * (size_t)(gFlightCount ? gFlightCount : 1));
//     if (!list) { printf("Out of memory!\n"); exit(1); }
//     for (i = 0; i < gFlightCount; i++) list[i] = &gFlights[i];
//     *n = gFlightCount;
//     return list;
// }

// static void printFlightTable(Flight **list, int n)
// {
//     int i;
//     if (n == 0) { printf("  (no flights)\n"); return; }
//     printf("\n  %-5s %-7s %-16s %-9s %-8s %-8s %s\n",
//            "ID", "FLIGHT", "AIRLINE", "ROUTE", "DEP UTC", "ARR UTC", "STATUS");
//     printf("  ---------------------------------------------------------------------------\n");
//     for (i = 0; i < n; i++) {
//         const Flight *f = list[i];
//         char d[16], a[16], st[32], route[12];
//         fmtUtc(d, actualDep(f));
//         fmtUtc(a, actualArr(f));
//         statusLabel(f, gClock, st);
//         sprintf(route, "%s>%s", f->origin, f->dest);
//         printf("  %-5d %-7s %-16.16s %-9s %-8s %-8s %s\n",
//                f->id, f->number, f->airline, route, d, a, st);
//     }
//     printf("  (%d flight%s)\n", n, n == 1 ? "" : "s");
// }

// static void showFlightDetail(const Flight *f)
// {
//     Airport  *o = findAirport(f->origin), *d = findAirport(f->dest);
//     Aircraft *a = findAircraft(f->reg);
//     Status    st = getStatus(f, gClock);
//     char t1[16], t2[16], t3[16], t4[16], lab[32];

//     statusLabel(f, gClock, lab);
//     printf("\n  ========== FLIGHT %s (ID %d) ==========\n", f->number, f->id);
//     printf("  Airline   : %s\n", f->airline);
//     if (a) printf("  Aircraft  : %s  [%s]  cruise %d km/h\n", a->model, a->reg, a->speed);
//     printf("  Status    : %s\n", lab);
//     printf("  Route     : %s (%s) -> %s (%s)\n", o->city, o->code, d->city, d->code);
//     printf("  Distance  : %d km\n", f->distKm);

//     fmtUtc(t1, f->depMin);   fmtLocal(t2, f->depMin, o);
//     printf("  Departure : %s (%s)  sched %s UTC / %s local at %s\n",
//            o->name, o->code, t1, t2, o->code);
//     if (f->delayMin > 0) {
//         fmtUtc(t3, actualDep(f)); fmtLocal(t4, actualDep(f), o);
//         printf("  Delayed   : %d min -> new departure %s UTC / %s local\n", f->delayMin, t3, t4);
//     }
//     fmtUtc(t1, actualArr(f)); fmtLocal(t2, actualArr(f), d);
//     printf("  Arrival   : %s (%s)  %s UTC / %s local at %s\n",
//            d->name, d->code, t1, t2, d->code);

//     if (st == ST_AIRBORNE) {
//         double p = flightProgress(f, gClock);
//         int eta = calcEta(f, gClock);
//         fmtUtc(t1, eta); fmtLocal(t2, eta, d);
//         printf("\n  --- LIVE ---\n");
//         printf("  Progress  : %.0f%%  (%.0f km flown, %.0f km to go)\n",
//                p * 100.0, f->distKm * p, f->distKm * (1.0 - p));
//         printf("  Speed     : %d km/h\n", currentSpeed(f, gClock));
//         printf("  Altitude  : %d ft\n", currentAltitude(f, gClock));
//         printf("  ETA       : %s UTC / %s local at %s (in %d min)\n",
//                t1, t2, d->code, eta - gClock);
//     } else if (st == ST_CANCELLED) {
//         printf("\n  This flight has been CANCELLED.\n");
//     }
// }

// /* ------------------------------------------------------------------ */
// /*  Airport board : ARRIVALS / DEPARTURES / ON GROUND                 */
// /* ------------------------------------------------------------------ */
// static void printBoard(const Airport *ap)
// {
//     char tl[16], tu[16];
//     int  i, n;

//     fmtLocal(tl, gClock, ap);
//     fmtUtc(tu, gClock);
//     printf("\n=============================================================================\n");
//     printf("  %s  -  %s, %s\n", ap->code, ap->name, ap->city);
//     printf("  Local time %s   |   UTC %s   |   (UTC%+.1f)\n", tl, tu, ap->utc);
//     printf("=============================================================================\n");

//     /* ---------------- ARRIVALS ---------------- */
//     printf("\n  >> ARRIVALS (inbound flights)\n");
//     printf("  %-7s %-16s %-5s %-8s %-8s %s\n", "FLIGHT", "AIRLINE", "FROM", "SCHED", "ETA", "STATUS");
//     printf("  -----------------------------------------------------------------------\n");
//     for (n = 0, i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         Status st;
//         char sc[16], eta[16], lab[32];
//         if (strcmp(f->dest, ap->code) != 0) continue;
//         st = getStatus(f, gClock);
//         if (st == ST_LANDED) continue;
//         fmtLocal(sc, f->depMin + f->durMin, ap);
//         if (st == ST_CANCELLED) strcpy(eta, "--");
//         else                    fmtLocal(eta, calcEta(f, gClock), ap);
//         statusLabel(f, gClock, lab);
//         printf("  %-7s %-16.16s %-5s %-8s %-8s %s\n",
//                f->number, f->airline, f->origin, sc, eta, lab);
//         n++;
//     }
//     if (!n) printf("  (none)\n");

//     /* ---------------- DEPARTURES ---------------- */
//     printf("\n  >> DEPARTURES\n");
//     printf("  %-7s %-16s %-5s %-8s %-8s %s\n", "FLIGHT", "AIRLINE", "TO", "SCHED", "EXPECTED", "STATUS");
//     printf("  -----------------------------------------------------------------------\n");
//     for (n = 0, i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         Status st;
//         char sc[16], ex[16], lab[32];
//         if (strcmp(f->origin, ap->code) != 0) continue;
//         st = getStatus(f, gClock);
//         if (st == ST_LANDED) continue;
//         fmtLocal(sc, f->depMin, ap);
//         if (st == ST_CANCELLED) strcpy(ex, "--");
//         else                    fmtLocal(ex, actualDep(f), ap);
//         if (st == ST_AIRBORNE)  strcpy(lab, "DEPARTED");
//         else                    statusLabel(f, gClock, lab);
//         printf("  %-7s %-16.16s %-5s %-8s %-8s %s\n",
//                f->number, f->airline, f->dest, sc, ex, lab);
//         n++;
//     }
//     if (!n) printf("  (none)\n");

//     /* ---------------- ON GROUND ---------------- */
//     printf("\n  >> ON GROUND (aircraft currently at %s)\n", ap->code);
//     printf("  %-7s %-16s %-9s %-18s %s\n", "FLIGHT", "AIRLINE", "REG", "AIRCRAFT", "STATE");
//     printf("  -----------------------------------------------------------------------\n");
//     for (n = 0, i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         Aircraft *ac = findAircraft(f->reg);
//         Status st = getStatus(f, gClock);
//         char state[64], when[16];
//         int  show = 0;

//         if (f->cancelled) continue;
//         if (strcmp(f->dest, ap->code) == 0 && st == ST_LANDED &&
//             gClock - actualArr(f) <= GROUND_WINDOW) {
//             sprintf(state, "Landed %d min ago (from %s)", gClock - actualArr(f), f->origin);
//             show = 1;
//         } else if (strcmp(f->origin, ap->code) == 0 &&
//                    (st == ST_SCHEDULED || st == ST_DELAYED) &&
//                    actualDep(f) - gClock <= GROUND_WINDOW) {
//             fmtLocal(when, actualDep(f), ap);
//             sprintf(state, "At gate - departs %s to %s", when, f->dest);
//             show = 1;
//         }
//         if (show) {
//             printf("  %-7s %-16.16s %-9s %-18.18s %s\n", f->number, f->airline,
//                    f->reg, ac ? ac->model : "?", state);
//             n++;
//         }
//     }
//     if (!n) printf("  (none)\n");
// }

// /* looks for airports by code / name / city; returns number of matches */
// static int searchAirports(const char *q, int *idx, int max)
// {
//     int i, n = 0;
//     for (i = 0; i < gAirportCount && n < max; i++) {
//         const Airport *a = &gAirports[i];
//         if (equalIC(a->code, q)) { idx[0] = i; return 1; }   /* exact code wins */
//     }
//     for (i = 0; i < gAirportCount && n < max; i++) {
//         const Airport *a = &gAirports[i];
//         if (containsIC(a->code, q) || containsIC(a->name, q) || containsIC(a->city, q))
//             idx[n++] = i;
//     }
//     return n;
// }

// static void airportSearch(void)
// {
//     char q[64], again[8];
//     int  idx[32], n, i, pick = -1;

//     printf("\n  Known airports:");
//     for (i = 0; i < gAirportCount; i++) printf(" %s", gAirports[i].code);
//     printf("\n");
//     readLine("  Search airport (code, name or city): ", q, sizeof q);
//     if (q[0] == '\0') return;

//     n = searchAirports(q, idx, 32);
//     if (n == 0) { printf("  [!] No airport matches \"%s\".\n", q); return; }
//     if (n == 1) pick = idx[0];
//     else {
//         char code[16];
//         printf("  Several matches:\n");
//         for (i = 0; i < n; i++)
//             printf("    %s - %s, %s\n", gAirports[idx[i]].code,
//                    gAirports[idx[i]].name, gAirports[idx[i]].city);
//         readLine("  Enter the airport code: ", code, sizeof code);
//         for (i = 0; i < n; i++)
//             if (equalIC(gAirports[idx[i]].code, code)) pick = idx[i];
//         if (pick < 0) { printf("  [!] Not one of the matches.\n"); return; }
//     }

//     for (;;) {
//         printBoard(&gAirports[pick]);
//         readLine("\n  [N] advance simulation 10 min, anything else = back: ", again, sizeof again);
//         if (gEof || (again[0] != 'n' && again[0] != 'N')) break;
//         gClock = (gClock + 10) % (2 * DAY);
//     }
// }

// /* ------------------------------------------------------------------ */
// /*  Feature functions                                                 */
// /* ------------------------------------------------------------------ */
// static void viewAllFlights(void)
// {
//     int n;
//     Flight **list = collectFlights(&n);
//     printFlightTable(list, n);
//     free(list);
// }

// static void searchFlights(void)
// {
//     char q[64];
//     int  i, n = 0;
//     Flight **list;
//     readLine("  Search flight number / airline / destination: ", q, sizeof q);
//     if (q[0] == '\0') return;
//     list = malloc(sizeof(Flight *) * (size_t)(gFlightCount ? gFlightCount : 1));
//     for (i = 0; i < gFlightCount; i++) {
//         Flight *f = &gFlights[i];
//         Airport *d = findAirport(f->dest);
//         if (containsIC(f->number, q) || containsIC(f->airline, q) ||
//             containsIC(f->dest, q) || (d && containsIC(d->city, q)))
//             list[n++] = f;
//     }
//     printFlightTable(list, n);
//     free(list);
// }

// static void findById(void)
// {
//     int id, pos;
//     if (!readIntRange("  Enter flight ID: ", 1, 999999, &id)) return;
//     pos = binarySearchId(0, gFlightCount - 1, id);      /* recursive */
//     if (pos < 0) printf("  [!] No flight with ID %d.\n", id);
//     else         showFlightDetail(&gFlights[pos]);
// }

// static int cmpFlightPtr(const void *a, const void *b)
// {
//     const Flight *x = *(Flight *const *)a;
//     const Flight *y = *(Flight *const *)b;
//     int r = 0;
//     switch (gSortKey) {
//         case 1: r = actualDep(x) - actualDep(y); break;
//         case 2: r = strcmp(x->airline, y->airline); break;
//         case 3: r = (int)getStatus(x, gClock) - (int)getStatus(y, gClock); break;
//     }
//     return r ? r : x->id - y->id;
// }

// static void sortAndFilter(void)
// {
//     int  c, i, n = 0;
//     Flight **all, **list;
//     char buf[64];

//     printf("\n  1. Sort by departure time\n  2. Sort by airline\n  3. Sort by status\n");
//     printf("  4. Filter by airline\n  5. Filter by status\n");
//     if (!readIntRange("  Choice: ", 1, 5, &c)) return;

//     all = collectFlights(&n);
//     if (c <= 3) {
//         gSortKey = c;
//         qsort(all, (size_t)n, sizeof(Flight *), cmpFlightPtr);
//         printFlightTable(all, n);
//         free(all);
//         return;
//     }

//     list = malloc(sizeof(Flight *) * (size_t)(n ? n : 1));
//     if (c == 4) {
//         int m = 0;
//         readLine("  Airline name (partial ok): ", buf, sizeof buf);
//         for (i = 0; i < n; i++)
//             if (containsIC(all[i]->airline, buf)) list[m++] = all[i];
//         printFlightTable(list, m);
//     } else {
//         int m = 0, found = -1;
//         readLine("  Status (scheduled/delayed/airborne/landed/cancelled): ", buf, sizeof buf);
//         for (i = 0; i < 5; i++)
//             if (equalIC(STATUS_TEXT[i], buf)) found = i;
//         if (found < 0) printf("  [!] Unknown status.\n");
//         else {
//             for (i = 0; i < n; i++)
//                 if ((int)getStatus(all[i], gClock) == found) list[m++] = all[i];
//             printFlightTable(list, m);
//         }
//     }
//     free(list);
//     free(all);
// }

// static void addFlight(void)
// {
//     Flight f;
//     char   buf[64];
//     int    v;

//     memset(&f, 0, sizeof f);
//     if (!readIntRange("  New flight ID: ", 1, 999999, &f.id)) return;
//     if (idExists(f.id)) { printf("  [!] Flight ID %d already exists.\n", f.id); return; }

//     readLine("  Flight number (e.g. PK305): ", f.number, sizeof f.number);
//     if (f.number[0] == '\0') { printf("  [!] Empty flight number.\n"); return; }
//     toUpper(f.number);

//     readLine("  Aircraft registration: ", f.reg, sizeof f.reg);
//     toUpper(f.reg);
//     {
//         Aircraft *a = findAircraft(f.reg);
//         if (!a) { printf("  [!] Unknown aircraft (see Aircraft menu).\n"); return; }
//         strcpy(f.airline, a->airline);
//     }

//     readLine("  Origin airport code: ", f.origin, sizeof f.origin);
//     readLine("  Destination airport code: ", f.dest, sizeof f.dest);
//     toUpper(f.origin); toUpper(f.dest);
//     if (!findAirport(f.origin) || !findAirport(f.dest)) { printf("  [!] Unknown airport code.\n"); return; }
//     if (strcmp(f.origin, f.dest) == 0) { printf("  [!] Origin and destination are the same.\n"); return; }

//     readLine("  Departure time UTC (HH:MM): ", buf, sizeof buf);
//     if (!parseHHMM(buf, &f.depMin)) { printf("  [!] Invalid time.\n"); return; }
//     if (!readIntRange("  Duration (minutes): ", 20, 1200, &f.durMin)) return;
//     if (!readIntRange("  Delay (minutes, 0 if none): ", 0, 600, &f.delayMin)) return;
//     if (!readIntRange("  Cancelled? (0 = no, 1 = yes): ", 0, 1, &v)) return;
//     f.cancelled = v;
//     if (!readIntRange("  Distance (km): ", 50, 20000, &f.distKm)) return;
//     if (!readIntRange("  Cruise altitude (ft): ", 10000, 45000, &f.cruiseAlt)) return;

//     if (gFlightCount == gFlightCap)
//         gFlights = grow(gFlights, &gFlightCap, sizeof(Flight));
//     gFlights[gFlightCount++] = f;
//     sortFlightsById();
//     printf("  Flight %s added. (Use Save to write it to %s)\n", f.number, FLIGHT_FILE);
// }

// static void trackFlight(void)
// {
//     char q[16];
//     int  i;
//     readLine("  Flight number to track: ", q, sizeof q);
//     for (i = 0; i < gFlightCount; i++)
//         if (equalIC(gFlights[i].number, q)) { showFlightDetail(&gFlights[i]); return; }
//     printf("  [!] Flight %s not found.\n", q);
// }

// static void showActiveAndCompleted(void)
// {
//     int i, n = 0;
//     char eta[16];

//     printf("\n  >> ACTIVE FLIGHTS (airborne now)\n");
//     printf("  %-7s %-9s %-16s %-8s %-9s %-8s %s\n",
//            "FLIGHT", "ROUTE", "AIRLINE", "SPEED", "ALT(ft)", "ETA UTC", "PROGRESS");
//     printf("  ----------------------------------------------------------------------------\n");
//     for (i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         char route[12];
//         if (getStatus(f, gClock) != ST_AIRBORNE) continue;
//         sprintf(route, "%s>%s", f->origin, f->dest);
//         fmtUtc(eta, calcEta(f, gClock));
//         printf("  %-7s %-9s %-16.16s %-4d km/h %-9d %-8s %.0f%%\n", f->number, route,
//                f->airline, currentSpeed(f, gClock), currentAltitude(f, gClock),
//                eta, flightProgress(f, gClock) * 100.0);
//         n++;
//     }
//     if (!n) printf("  (none)\n");

//     printf("\n  >> COMPLETED FLIGHTS (landed)\n");
//     printf("  %-7s %-9s %-16s %s\n", "FLIGHT", "ROUTE", "AIRLINE", "LANDED AT (UTC)");
//     printf("  -----------------------------------------------------------\n");
//     for (n = 0, i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         char route[12];
//         if (getStatus(f, gClock) != ST_LANDED) continue;
//         sprintf(route, "%s>%s", f->origin, f->dest);
//         fmtUtc(eta, actualArr(f));
//         printf("  %-7s %-9s %-16.16s %s\n", f->number, route, f->airline, eta);
//         n++;
//     }
//     if (!n) printf("  (none)\n");
// }

// static void showDelayed(void)
// {
//     int i, n = 0;
//     printf("\n  %-7s %-16s %-9s %-10s %-10s %s\n",
//            "FLIGHT", "AIRLINE", "ROUTE", "SCHED UTC", "NEW UTC", "DELAY");
//     printf("  ----------------------------------------------------------------\n");
//     for (i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         char route[12], s[16], a[16];
//         if (f->delayMin <= 0 || f->cancelled) continue;
//         sprintf(route, "%s>%s", f->origin, f->dest);
//         fmtUtc(s, f->depMin);
//         fmtUtc(a, actualDep(f));
//         printf("  %-7s %-16.16s %-9s %-10s %-10s %d min\n",
//                f->number, f->airline, route, s, a, f->delayMin);
//         n++;
//     }
//     if (!n) printf("  (no delayed flights)\n");
// }

// static void showRoutes(void)
// {
//     int i;
//     printf("\n  %-7s %-34s %-34s\n", "FLIGHT", "DEPARTURE (airport, local time)", "ARRIVAL (airport, local time)");
//     printf("  ------------------------------------------------------------------------------\n");
//     for (i = 0; i < gFlightCount; i++) {
//         const Flight *f = &gFlights[i];
//         Airport *o = findAirport(f->origin), *d = findAirport(f->dest);
//         char t1[16], t2[16], l1[64], l2[64];
//         fmtLocal(t1, actualDep(f), o);
//         fmtLocal(t2, actualArr(f), d);
//         sprintf(l1, "%s %s %s", o->code, o->city, t1);
//         sprintf(l2, "%s %s %s", d->code, d->city, t2);
//         printf("  %-7s %-34s %-34s\n", f->number, l1, l2);
//     }
// }

// static void listAircraft(void)
// {
//     int i, j;
//     printf("\n  %-9s %-18s %-16s %-9s %s\n", "REG", "MODEL", "AIRLINE", "SPEED", "ASSIGNED FLIGHT(S)");
//     printf("  ----------------------------------------------------------------------------\n");
//     for (i = 0; i < gAircraftCount; i++) {
//         const Aircraft *a = &gAircraft[i];
//         printf("  %-9s %-18.18s %-16.16s %-4d km/h ", a->reg, a->model, a->airline, a->speed);
//         for (j = 0; j < gFlightCount; j++)
//             if (equalIC(gFlights[j].reg, a->reg)) printf("%s ", gFlights[j].number);
//         printf("\n");
//     }
// }

// static void searchAircraft(void)
// {
//     char q[32];
//     int  i, n = 0;
//     readLine("  Search registration / model / airline: ", q, sizeof q);
//     if (q[0] == '\0') return;
//     printf("\n  %-9s %-18s %-16s %s\n", "REG", "MODEL", "AIRLINE", "SPEED");
//     for (i = 0; i < gAircraftCount; i++) {
//         const Aircraft *a = &gAircraft[i];
//         if (containsIC(a->reg, q) || containsIC(a->model, q) || containsIC(a->airline, q)) {
//             printf("  %-9s %-18.18s %-16.16s %d km/h\n", a->reg, a->model, a->airline, a->speed);
//             n++;
//         }
//     }
//     if (!n) printf("  (no aircraft found)\n");
// }

// static void showStatistics(void)
// {
//     int counts[5] = {0, 0, 0, 0, 0}, i, delayed = 0, totalDelay = 0;
//     for (i = 0; i < gFlightCount; i++) {
//         counts[getStatus(&gFlights[i], gClock)]++;
//         if (gFlights[i].delayMin > 0 && !gFlights[i].cancelled) {
//             delayed++;
//             totalDelay += gFlights[i].delayMin;
//         }
//     }
//     printf("\n  ===== FLIGHT STATISTICS =====\n");
//     printf("  Total flights     : %d\n", gFlightCount);
//     printf("  Active (airborne) : %d\n", counts[ST_AIRBORNE]);
//     printf("  Scheduled         : %d\n", counts[ST_SCHEDULED]);
//     printf("  Delayed (waiting) : %d\n", counts[ST_DELAYED]);
//     printf("  Completed (landed): %d\n", counts[ST_LANDED]);
//     printf("  Cancelled         : %d\n", counts[ST_CANCELLED]);
//     printf("  Flights with delay: %d", delayed);
//     if (delayed) printf("  (average %d min)", totalDelay / delayed);
//     printf("\n");
// }

// static void showHistory(void)
// {
//     int i, n = 0;
//     Flight **list = malloc(sizeof(Flight *) * (size_t)(gFlightCount ? gFlightCount : 1));
//     for (i = 0; i < gFlightCount; i++) {
//         Status st = getStatus(&gFlights[i], gClock);
//         if (st == ST_LANDED || st == ST_CANCELLED) list[n++] = &gFlights[i];
//     }
//     printf("\n  >> FLIGHT HISTORY (landed + cancelled)");
//     printFlightTable(list, n);
//     free(list);
// }

// static int zoneOffset(const char *s, float *off)
// {
//     Airport *a;
//     if (equalIC(s, "UTC"))                         { *off = 0.0f;      return 1; }
//     if (equalIC(s, "LOC") || equalIC(s, "LOCAL"))  { *off = LOCAL_UTC; return 1; }
//     a = findAirport(s);
//     if (a) { *off = a->utc; return 1; }
//     return 0;
// }

// static void timeZoneConverter(void)
// {
//     char  from[16], to[16], tm[16];
//     float fo, tof;
//     int   mins, utc, target, shift = 0;

//     printf("\n  Zones: UTC, LOC (local, UTC%+.0f) or any airport code (e.g. DXB)\n", LOCAL_UTC);
//     readLine("  Convert FROM zone: ", from, sizeof from);
//     if (!zoneOffset(from, &fo)) { printf("  [!] Unknown zone.\n"); return; }
//     readLine("  Time there (HH:MM): ", tm, sizeof tm);
//     if (!parseHHMM(tm, &mins)) { printf("  [!] Invalid time.\n"); return; }
//     readLine("  Convert TO zone: ", to, sizeof to);
//     if (!zoneOffset(to, &tof)) { printf("  [!] Unknown zone.\n"); return; }

//     utc    = mins - (int)(fo * 60);
//     target = utc + (int)(tof * 60);
//     if (target < 0)          { target += DAY; shift = -1; }
//     else if (target >= DAY)  { target -= DAY; shift = 1; }

//     printf("\n  %s %s  =  UTC %02d:%02d  =  %s %02d:%02d%s\n",
//            from, tm, ((utc % DAY) + DAY) % DAY / 60, ((utc % DAY) + DAY) % DAY % 60,
//            to, target / 60, target % 60,
//            shift > 0 ? " (next day)" : shift < 0 ? " (previous day)" : "");
// }

// static void simulationClock(void)
// {
//     int  c, v;
//     char buf[32], t[16];

//     fmtUtc(t, gClock);
//     printf("\n  Simulation clock: %s UTC\n", t);
//     printf("  1. Advance by N minutes\n  2. Set time (HH:MM UTC)\n");
//     if (!readIntRange("  Choice: ", 1, 2, &c)) return;
//     if (c == 1) {
//         if (!readIntRange("  Minutes to advance: ", 1, DAY, &v)) return;
//         gClock = (gClock + v) % (2 * DAY);
//     } else {
//         readLine("  New time (HH:MM): ", buf, sizeof buf);
//         if (!parseHHMM(buf, &v)) { printf("  [!] Invalid time.\n"); return; }
//         gClock = v;
//     }
//     fmtUtc(t, gClock);
//     printf("  Clock is now %s UTC.\n", t);
// }

// /* ------------------------------------------------------------------ */
// /*  Menus                                                             */
// /* ------------------------------------------------------------------ */
// static void menuFlights(void)
// {
//     int c;
//     do {
//         printf("\n --- FLIGHTS ---\n");
//         printf("  1. View all flights\n  2. Search (flight no / airline / destination)\n");
//         printf("  3. Find by flight ID (binary search)\n  4. Sort & filter\n  5. Add a flight\n  0. Back\n");
//         if (!readIntRange(" Choice: ", 0, 5, &c)) { c = -1; continue; }
//         switch (c) {
//             case 1: viewAllFlights(); break;
//             case 2: searchFlights();  break;
//             case 3: findById();       break;
//             case 4: sortAndFilter();  break;
//             case 5: addFlight();      break;
//         }
//         if (c > 0) pressEnter();
//     } while (c != 0 && !gEof);
// }

// static void menuAircraft(void)
// {
//     int c;
//     do {
//         printf("\n --- AIRCRAFT ---\n");
//         printf("  1. List all aircraft\n  2. Search aircraft\n  3. Speed & altitude of airborne flights\n  0. Back\n");
//         if (!readIntRange(" Choice: ", 0, 3, &c)) { c = -1; continue; }
//         switch (c) {
//             case 1: listAircraft();           break;
//             case 2: searchAircraft();         break;
//             case 3: showActiveAndCompleted(); break;
//         }
//         if (c > 0) pressEnter();
//     } while (c != 0 && !gEof);
// }

// static void menuTracking(void)
// {
//     int c;
//     do {
//         printf("\n --- TRACKING ---\n");
//         printf("  1. Airport search (arrivals / departures / on ground)\n");
//         printf("  2. Track a flight (live details + ETA)\n");
//         printf("  3. Active & completed flights\n");
//         printf("  4. Delayed flights\n");
//         printf("  5. Flight routes & airport timings\n  0. Back\n");
//         if (!readIntRange(" Choice: ", 0, 5, &c)) { c = -1; continue; }
//         switch (c) {
//             case 1: airportSearch();          break;
//             case 2: trackFlight();            break;
//             case 3: showActiveAndCompleted(); break;
//             case 4: showDelayed();            break;
//             case 5: showRoutes();             break;
//         }
//         if (c > 1) pressEnter();
//     } while (c != 0 && !gEof);
// }

// static void menuStatistics(void)
// {
//     int c;
//     do {
//         printf("\n --- STATISTICS ---\n");
//         printf("  1. Flight statistics\n  2. Flight history\n  3. Time zone converter\n  0. Back\n");
//         if (!readIntRange(" Choice: ", 0, 3, &c)) { c = -1; continue; }
//         switch (c) {
//             case 1: showStatistics();     break;
//             case 2: showHistory();        break;
//             case 3: timeZoneConverter();  break;
//         }
//         if (c > 0) pressEnter();
//     } while (c != 0 && !gEof);
// }

// int main(void)
// {
//     int  c;
//     char t[16];

//     printf("=============================================\n");
//     printf("        FLIGHT RADAR SYSTEM  (simulator)\n");
//     printf("=============================================\n");
//     loadAll();
//     if (gFlightCount == 0) {
//         printf("  No flight data loaded. Make sure the .txt files are in this folder.\n");
//         return 1;
//     }

//     do {
//         fmtUtc(t, gClock);
//         printf("\n=========== MAIN MENU  [sim time %s UTC] ===========\n", t);
//         printf("  1. Flights\n  2. Aircraft\n  3. Tracking\n  4. Statistics\n");
//         printf("  5. Simulation clock\n  6. Save flights to file\n  7. Reload data from files\n  0. Exit\n");
//         if (!readIntRange(" Choice: ", 0, 7, &c)) { c = -1; continue; }
//         switch (c) {
//             case 1: menuFlights();      break;
//             case 2: menuAircraft();     break;
//             case 3: menuTracking();     break;
//             case 4: menuStatistics();   break;
//             case 5: simulationClock();  break;
//             case 6: if (saveFlights()) printf("  Saved %d flights to %s\n", gFlightCount, FLIGHT_FILE); break;
//             case 7: loadAll();          break;
//         }
//     } while (c != 0 && !gEof);

//     free(gAirports); free(gAircraft); free(gFlights);
//     printf("\nGoodbye!\n");
//     return 0;
// }

