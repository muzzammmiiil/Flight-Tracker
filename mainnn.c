#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define AIRPORT_FILE  "airports.txt"
#define AIRCRAFT_FILE "aircraft.txt"
#define FLIGHT_FILE   "flights.txt"

#define MAX_AIRPORTS 50
#define MAX_AIRCRAFT 50
#define MAX_FLIGHTS  200

#define LOCAL_UTC     5.0   /* our own time zone (Pakistan, UTC+5)       */
#define DAY           1440  /* minutes in one day                        */
#define GROUND_WINDOW 60    /* minutes a plane counts as "on the ground" */
#define DEFAULT_SPEED 850

/* flight status values */
#define SCHEDULED 0
#define DELAYED   1
#define AIRBORNE  2
#define LANDED    3
#define CANCELLED 4

/* ------------------------------------------------------------------ */
/*  Data                                                              */
/* ------------------------------------------------------------------ */
struct Airport {
    char  code[4];
    char  name[40];
    char  city[30];
    float utc;               /* hours from UTC */
};

struct Aircraft {
    char reg[12];
    char model[24];
    char airline[30];
    int  speed;              /* km/h */
};

struct Flight {
    int  id;
    char number[10];
    char airline[30];
    char reg[12];
    char origin[4];
    char dest[4];
    int  depMin;             /* departure, minutes since 00:00 UTC */
    int  durMin;             /* duration in minutes                */
    int  delayMin;
    int  cancelled;          /* 0 or 1 */
    int  distKm;
    int  cruiseAlt;          /* feet */
};

struct Airport  airports[MAX_AIRPORTS];  int airportCount = 0;
struct Aircraft aircraft[MAX_AIRCRAFT];  int aircraftCount = 0;
struct Flight   flights[MAX_FLIGHTS];    int flightCount = 0;

const char *statusNames[5] = { "SCHEDULED", "DELAYED", "AIRBORNE", "LANDED", "CANCELLED" };

int clockMin = 600;          /* simulated time in UTC minutes (10:00) */
int endOfInput = 0;          /* becomes 1 when the input ends         */

/* ------------------------------------------------------------------ */
/*  Small helper functions                                            */
/* ------------------------------------------------------------------ */
void toUpper(char s[])
{
    int i;
    for (i = 0; s[i] != '\0'; i++)
        s[i] = toupper((unsigned char)s[i]);
}

/* 1 if text contains word (ignoring upper/lower case) */
int containsIC(const char text[], const char word[])
{
    char a[128], b[128];
    strncpy(a, text, 127); a[127] = '\0';
    strncpy(b, word, 127); b[127] = '\0';
    toUpper(a);
    toUpper(b);
    return strstr(a, b) != NULL;
}

/* 1 if both texts are equal (ignoring upper/lower case) */
int equalIC(const char x[], const char y[])
{
    char a[128], b[128];
    strncpy(a, x, 127); a[127] = '\0';
    strncpy(b, y, 127); b[127] = '\0';
    toUpper(a);
    toUpper(b);
    return strcmp(a, b) == 0;
}

/* 1 for empty lines and comment lines (starting with #) */
int isBlankLine(const char s[])
{
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    return s[i] == '\0' || s[i] == '\n' || s[i] == '\r' || s[i] == '#';
}

void readLine(const char prompt[], char buf[], int size)
{
    printf("%s", prompt);
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        endOfInput = 1;
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';   /* remove the newline */
}

/* asks for a whole number between low and high; returns 1 if valid */
int readInt(const char prompt[], int low, int high, int *result)
{
    char text[64], *end;
    long value;

    readLine(prompt, text, 64);
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < low || value > high) {
        printf("  [!] Invalid input (expected %d to %d).\n", low, high);
        return 0;
    }
    *result = (int)value;
    return 1;
}

/* converts "HH:MM" to minutes; returns 1 if valid */
int parseTime(const char text[], int *minutes)
{
    int h, m;
    char extra;
    if (sscanf(text, " %d:%d %c", &h, &m, &extra) != 2)
        return 0;
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return 0;
    *minutes = h * 60 + m;
    return 1;
}

void pressEnter(void)
{
    char tmp[8];
    readLine("\nPress Enter to continue...", tmp, 8);
}

/* ------------------------------------------------------------------ */
/*  Time formatting (+1 = next day, -1 = previous day)                */
/* ------------------------------------------------------------------ */
void formatTime(char buf[], int minutes)
{
    int day = 0;
    while (minutes < 0)    { minutes += DAY; day--; }
    while (minutes >= DAY) { minutes -= DAY; day++; }

    if (day > 0)       sprintf(buf, "%02d:%02d+%d", minutes / 60, minutes % 60, day);
    else if (day < 0)  sprintf(buf, "%02d:%02d%d", minutes / 60, minutes % 60, day);
    else               sprintf(buf, "%02d:%02d", minutes / 60, minutes % 60);
}

/* shows a UTC time as local time of airport number a */
void formatLocal(char buf[], int utcMinutes, int a)
{
    int offset;
    if (airports[a].utc >= 0)
        offset = (int)(airports[a].utc * 60 + 0.5);
    else
        offset = (int)(airports[a].utc * 60 - 0.5);
    formatTime(buf, utcMinutes + offset);
}

/* ------------------------------------------------------------------ */
/*  Searching the data (these return an index, or -1 if not found)    */
/* ------------------------------------------------------------------ */
int findAirport(const char code[])
{
    int i;
    for (i = 0; i < airportCount; i++)
        if (equalIC(airports[i].code, code))
            return i;
    return -1;
}

int findAircraft(const char reg[])
{
    int i;
    for (i = 0; i < aircraftCount; i++)
        if (equalIC(aircraft[i].reg, reg))
            return i;
    return -1;
}

int idExists(int id)
{
    int i;
    for (i = 0; i < flightCount; i++)
        if (flights[i].id == id)
            return 1;
    return 0;
}

/* recursive binary search (flights must be sorted by id) */
int binarySearch(int low, int high, int id)
{
    int mid;
    if (low > high)
        return -1;
    mid = (low + high) / 2;
    if (flights[mid].id == id)
        return mid;
    if (flights[mid].id < id)
        return binarySearch(mid + 1, high, id);
    return binarySearch(low, mid - 1, id);
}

/* insertion sort of all flights by id */
void sortFlightsById(void)
{
    int i, j;
    struct Flight temp;
    for (i = 1; i < flightCount; i++) {
        temp = flights[i];
        j = i - 1;
        while (j >= 0 && flights[j].id > temp.id) {
            flights[j + 1] = flights[j];
            j--;
        }
        flights[j + 1] = temp;
    }
}

/* ------------------------------------------------------------------ */
/*  Flight simulation                                                 */
/* ------------------------------------------------------------------ */
int actualDep(int f) { return flights[f].depMin + flights[f].delayMin; }
int actualArr(int f) { return actualDep(f) + flights[f].durMin; }

int getStatus(int f, int now)
{
    if (flights[f].cancelled)
        return CANCELLED;
    if (now < actualDep(f)) {
        if (flights[f].delayMin > 0)
            return DELAYED;
        return SCHEDULED;
    }
    if (now < actualArr(f))
        return AIRBORNE;
    return LANDED;
}

/* how much of the flight is done: 0.0 to 1.0 */
double flightProgress(int f, int now)
{
    double p = (double)(now - actualDep(f)) / flights[f].durMin;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    return p;
}

int cruiseSpeedOf(int f)
{
    int a = findAircraft(flights[f].reg);
    if (a < 0)
        return DEFAULT_SPEED;
    return aircraft[a].speed;
}

/* climbs during the first 15%, descends during the last 15% */
int currentAltitude(int f, int now)
{
    double p = flightProgress(f, now);
    if (p < 0.15) return (int)(flights[f].cruiseAlt * p / 0.15);
    if (p > 0.85) return (int)(flights[f].cruiseAlt * (1.0 - p) / 0.15);
    return flights[f].cruiseAlt;
}

int currentSpeed(int f, int now)
{
    double ratio = (double)currentAltitude(f, now) / flights[f].cruiseAlt;
    return (int)(cruiseSpeedOf(f) * (0.6 + 0.4 * ratio));
}

/* ETA = now + (distance left / speed) while flying, else planned arrival */
int calcEta(int f, int now)
{
    double left;
    if (getStatus(f, now) == AIRBORNE) {
        left = flights[f].distKm * (1.0 - flightProgress(f, now));
        return now + (int)(left / cruiseSpeedOf(f) * 60.0 + 0.5);
    }
    return actualArr(f);
}

void statusLabel(int f, char buf[])
{
    int st = getStatus(f, clockMin);
    if (st == DELAYED)
        sprintf(buf, "DELAYED +%dm", flights[f].delayMin);
    else
        strcpy(buf, statusNames[st]);
}

/* ------------------------------------------------------------------ */
/*  Loading and saving files                                          */
/* ------------------------------------------------------------------ */
void loadAirports(void)
{
    FILE *fp = fopen(AIRPORT_FILE, "r");
    char line[256];
    int lineNo = 0;

    if (fp == NULL) { printf("  [!] Cannot open %s\n", AIRPORT_FILE); return; }
    while (fgets(line, sizeof line, fp) != NULL) {
        struct Airport a;
        lineNo++;
        if (isBlankLine(line)) continue;
        if (sscanf(line, " %3[^|]|%39[^|]|%29[^|]|%f", a.code, a.name, a.city, &a.utc) != 4) {
            printf("  [!] %s line %d skipped (bad format)\n", AIRPORT_FILE, lineNo);
            continue;
        }
        toUpper(a.code);
        if (findAirport(a.code) >= 0) {
            printf("  [!] %s line %d skipped (duplicate code)\n", AIRPORT_FILE, lineNo);
            continue;
        }
        if (airportCount == MAX_AIRPORTS) { printf("  [!] Too many airports.\n"); break; }
        airports[airportCount] = a;
        airportCount++;
    }
    fclose(fp);
}

void loadAircraft(void)
{
    FILE *fp = fopen(AIRCRAFT_FILE, "r");
    char line[256];
    int lineNo = 0;

    if (fp == NULL) { printf("  [!] Cannot open %s\n", AIRCRAFT_FILE); return; }
    while (fgets(line, sizeof line, fp) != NULL) {
        struct Aircraft a;
        lineNo++;
        if (isBlankLine(line)) continue;
        if (sscanf(line, " %11[^|]|%23[^|]|%29[^|]|%d", a.reg, a.model, a.airline, &a.speed) != 4
            || a.speed <= 0) {
            printf("  [!] %s line %d skipped (bad format)\n", AIRCRAFT_FILE, lineNo);
            continue;
        }
        toUpper(a.reg);
        if (findAircraft(a.reg) >= 0) {
            printf("  [!] %s line %d skipped (duplicate reg)\n", AIRCRAFT_FILE, lineNo);
            continue;
        }
        if (aircraftCount == MAX_AIRCRAFT) { printf("  [!] Too many aircraft.\n"); break; }
        aircraft[aircraftCount] = a;
        aircraftCount++;
    }
    fclose(fp);
}

void loadFlights(void)
{
    FILE *fp = fopen(FLIGHT_FILE, "r");
    char line[256];
    int lineNo = 0;

    if (fp == NULL) { printf("  [!] Cannot open %s\n", FLIGHT_FILE); return; }
    while (fgets(line, sizeof line, fp) != NULL) {
        struct Flight f;
        int h, m;
        lineNo++;
        if (isBlankLine(line)) continue;
        if (sscanf(line, " %d|%9[^|]|%29[^|]|%11[^|]|%3[^|]|%3[^|]|%d:%d|%d|%d|%d|%d|%d",
                   &f.id, f.number, f.airline, f.reg, f.origin, f.dest, &h, &m,
                   &f.durMin, &f.delayMin, &f.cancelled, &f.distKm, &f.cruiseAlt) != 13) {
            printf("  [!] %s line %d skipped (bad format)\n", FLIGHT_FILE, lineNo);
            continue;
        }
        toUpper(f.origin); toUpper(f.dest); toUpper(f.reg);
        f.depMin = h * 60 + m;

        if (f.id <= 0 || h < 0 || h > 23 || m < 0 || m > 59 || f.durMin <= 0 ||
            f.delayMin < 0 || (f.cancelled != 0 && f.cancelled != 1) ||
            f.distKm <= 0 || f.cruiseAlt <= 0 || strcmp(f.origin, f.dest) == 0) {
            printf("  [!] %s line %d skipped (invalid values)\n", FLIGHT_FILE, lineNo);
            continue;
        }
        if (idExists(f.id)) {
            printf("  [!] %s line %d skipped (duplicate flight ID)\n", FLIGHT_FILE, lineNo);
            continue;
        }
        if (findAirport(f.origin) < 0 || findAirport(f.dest) < 0) {
            printf("  [!] %s line %d skipped (unknown airport)\n", FLIGHT_FILE, lineNo);
            continue;
        }
        if (findAircraft(f.reg) < 0) {
            printf("  [!] %s line %d skipped (unknown aircraft)\n", FLIGHT_FILE, lineNo);
            continue;
        }
        if (flightCount == MAX_FLIGHTS) { printf("  [!] Too many flights.\n"); break; }
        flights[flightCount] = f;
        flightCount++;
    }
    fclose(fp);
    sortFlightsById();
}

void loadAll(void)
{
    airportCount = 0;
    aircraftCount = 0;
    flightCount = 0;
    loadAirports();
    loadAircraft();
    loadFlights();
    printf("  Loaded: %d airports, %d aircraft, %d flights.\n",
           airportCount, aircraftCount, flightCount);
}

void saveFlights(void)
{
    FILE *fp = fopen(FLIGHT_FILE, "w");
    int i;

    if (fp == NULL) { printf("  [!] Cannot write %s\n", FLIGHT_FILE); return; }
    fprintf(fp, "# id|flight no|airline|aircraft reg|origin|dest|dep UTC HH:MM|"
                "duration min|delay min|cancelled(0/1)|distance km|cruise alt ft\n");
    for (i = 0; i < flightCount; i++) {
        struct Flight f = flights[i];
        fprintf(fp, "%d|%s|%s|%s|%s|%s|%02d:%02d|%d|%d|%d|%d|%d\n",
                f.id, f.number, f.airline, f.reg, f.origin, f.dest,
                f.depMin / 60, f.depMin % 60, f.durMin, f.delayMin,
                f.cancelled, f.distKm, f.cruiseAlt);
    }
    fclose(fp);
    printf("  Saved %d flights to %s\n", flightCount, FLIGHT_FILE);
}

/* ------------------------------------------------------------------ */
/*  Showing flights                                                   */
/* ------------------------------------------------------------------ */

/* prints the flights whose numbers are stored in list[0..n-1] */
void printFlightTable(int list[], int n)
{
    int i, f;
    char dep[16], arr[16], status[32];

    if (n == 0) { printf("  (no flights)\n"); return; }
    printf("\n  %-5s %-7s %-16s %-9s %-8s %-8s %s\n",
           "ID", "FLIGHT", "AIRLINE", "ROUTE", "DEP UTC", "ARR UTC", "STATUS");
    printf("  --------------------------------------------------------------------------\n");
    for (i = 0; i < n; i++) {
        f = list[i];
        formatTime(dep, actualDep(f));
        formatTime(arr, actualArr(f));
        statusLabel(f, status);
        printf("  %-5d %-7s %-16.16s %s>%s     %-8s %-8s %s\n",
               flights[f].id, flights[f].number, flights[f].airline,
               flights[f].origin, flights[f].dest, dep, arr, status);
    }
    printf("  (%d flight%s)\n", n, n == 1 ? "" : "s");
}

void showFlightDetail(int f)
{
    int o = findAirport(flights[f].origin);
    int d = findAirport(flights[f].dest);
    int a = findAircraft(flights[f].reg);
    int st = getStatus(f, clockMin);
    char t1[16], t2[16], label[32];

    statusLabel(f, label);
    printf("\n  ========== FLIGHT %s (ID %d) ==========\n", flights[f].number, flights[f].id);
    printf("  Airline   : %s\n", flights[f].airline);
    if (a >= 0)
        printf("  Aircraft  : %s  [%s]  cruise %d km/h\n", aircraft[a].model, aircraft[a].reg, aircraft[a].speed);
    printf("  Status    : %s\n", label);
    printf("  Route     : %s (%s) -> %s (%s)\n", airports[o].city, airports[o].code,
           airports[d].city, airports[d].code);
    printf("  Distance  : %d km\n", flights[f].distKm);

    formatTime(t1, flights[f].depMin);
    formatLocal(t2, flights[f].depMin, o);
    printf("  Departure : %s (%s)  sched %s UTC / %s local\n", airports[o].name, airports[o].code, t1, t2);
    if (flights[f].delayMin > 0) {
        formatTime(t1, actualDep(f));
        formatLocal(t2, actualDep(f), o);
        printf("  Delayed   : %d min -> new departure %s UTC / %s local\n", flights[f].delayMin, t1, t2);
    }
    formatTime(t1, actualArr(f));
    formatLocal(t2, actualArr(f), d);
    printf("  Arrival   : %s (%s)  %s UTC / %s local\n", airports[d].name, airports[d].code, t1, t2);

    if (st == AIRBORNE) {
        double p = flightProgress(f, clockMin);
        int eta = calcEta(f, clockMin);
        formatTime(t1, eta);
        formatLocal(t2, eta, d);
        printf("\n  --- LIVE ---\n");
        printf("  Progress  : %.0f%%  (%.0f km flown, %.0f km to go)\n",
               p * 100, flights[f].distKm * p, flights[f].distKm * (1 - p));
        printf("  Speed     : %d km/h\n", currentSpeed(f, clockMin));
        printf("  Altitude  : %d ft\n", currentAltitude(f, clockMin));
        printf("  ETA       : %s UTC / %s local (in %d min)\n", t1, t2, eta - clockMin);
    }
    if (st == CANCELLED)
        printf("\n  This flight has been CANCELLED.\n");
}

/* ------------------------------------------------------------------ */
/*  Airport board: arrivals, departures, aircraft on the ground       */
/* ------------------------------------------------------------------ */
void printBoard(int a)
{
    int i, count, st, m;
    char timeLocal[16], timeUtc[16], sched[16], other[16], label[32], state[64];

    formatLocal(timeLocal, clockMin, a);
    formatTime(timeUtc, clockMin);
    printf("\n=============================================================================\n");
    printf("  %s  -  %s, %s\n", airports[a].code, airports[a].name, airports[a].city);
    printf("  Local time %s   |   UTC %s   |   (UTC%+.1f)\n", timeLocal, timeUtc, airports[a].utc);
    printf("=============================================================================\n");

    /* ----- arrivals ----- */
    printf("\n  >> ARRIVALS (inbound flights)\n");
    printf("  %-7s %-16s %-5s %-8s %-8s %s\n", "FLIGHT", "AIRLINE", "FROM", "SCHED", "ETA", "STATUS");
    count = 0;
    for (i = 0; i < flightCount; i++) {
        if (strcmp(flights[i].dest, airports[a].code) != 0) continue;
        st = getStatus(i, clockMin);
        if (st == LANDED) continue;
        formatLocal(sched, flights[i].depMin + flights[i].durMin, a);
        if (st == CANCELLED) strcpy(other, "--");
        else                 formatLocal(other, calcEta(i, clockMin), a);
        statusLabel(i, label);
        printf("  %-7s %-16.16s %-5s %-8s %-8s %s\n", flights[i].number, flights[i].airline,
               flights[i].origin, sched, other, label);
        count++;
    }
    if (count == 0) printf("  (none)\n");

    /* ----- departures ----- */
    printf("\n  >> DEPARTURES\n");
    printf("  %-7s %-16s %-5s %-8s %-8s %s\n", "FLIGHT", "AIRLINE", "TO", "SCHED", "EXPECTED", "STATUS");
    count = 0;
    for (i = 0; i < flightCount; i++) {
        if (strcmp(flights[i].origin, airports[a].code) != 0) continue;
        st = getStatus(i, clockMin);
        if (st == LANDED) continue;
        formatLocal(sched, flights[i].depMin, a);
        if (st == CANCELLED) strcpy(other, "--");
        else                 formatLocal(other, actualDep(i), a);
        if (st == AIRBORNE)  strcpy(label, "DEPARTED");
        else                 statusLabel(i, label);
        printf("  %-7s %-16.16s %-5s %-8s %-8s %s\n", flights[i].number, flights[i].airline,
               flights[i].dest, sched, other, label);
        count++;
    }
    if (count == 0) printf("  (none)\n");

    /* ----- on the ground ----- */
    printf("\n  >> ON GROUND (aircraft currently at %s)\n", airports[a].code);
    printf("  %-7s %-16s %-9s %-18s %s\n", "FLIGHT", "AIRLINE", "REG", "AIRCRAFT", "STATE");
    count = 0;
    for (i = 0; i < flightCount; i++) {
        if (flights[i].cancelled) continue;
        st = getStatus(i, clockMin);
        state[0] = '\0';

        if (strcmp(flights[i].dest, airports[a].code) == 0 && st == LANDED &&
            clockMin - actualArr(i) <= GROUND_WINDOW) {
            sprintf(state, "Landed %d min ago (from %s)", clockMin - actualArr(i), flights[i].origin);
        }
        if (strcmp(flights[i].origin, airports[a].code) == 0 &&
            (st == SCHEDULED || st == DELAYED) && actualDep(i) - clockMin <= GROUND_WINDOW) {
            formatLocal(sched, actualDep(i), a);
            sprintf(state, "At gate - departs %s to %s", sched, flights[i].dest);
        }
        if (state[0] != '\0') {
            m = findAircraft(flights[i].reg);
            printf("  %-7s %-16.16s %-9s %-18.18s %s\n", flights[i].number, flights[i].airline,
                   flights[i].reg, m >= 0 ? aircraft[m].model : "?", state);
            count++;
        }
    }
    if (count == 0) printf("  (none)\n");
}

void airportSearch(void)
{
    char query[64], code[16], again[8];
    int matches[MAX_AIRPORTS], n = 0, i, pick = -1;

    printf("\n  Known airports:");
    for (i = 0; i < airportCount; i++)
        printf(" %s", airports[i].code);
    printf("\n");
    readLine("  Search airport (code, name or city): ", query, 64);
    if (query[0] == '\0') return;

    /* an exact code always wins */
    pick = findAirport(query);
    if (pick < 0) {
        for (i = 0; i < airportCount; i++)
            if (containsIC(airports[i].code, query) || containsIC(airports[i].name, query) ||
                containsIC(airports[i].city, query))
                matches[n++] = i;
        if (n == 0) { printf("  [!] No airport matches \"%s\".\n", query); return; }
        if (n == 1) {
            pick = matches[0];
        } else {
            printf("  Several matches:\n");
            for (i = 0; i < n; i++)
                printf("    %s - %s, %s\n", airports[matches[i]].code,
                       airports[matches[i]].name, airports[matches[i]].city);
            readLine("  Enter the airport code: ", code, 16);
            for (i = 0; i < n; i++)
                if (equalIC(airports[matches[i]].code, code))
                    pick = matches[i];
            if (pick < 0) { printf("  [!] Not one of the matches.\n"); return; }
        }
    }

    while (1) {
        printBoard(pick);
        readLine("\n  [N] advance simulation 10 min, anything else = back: ", again, 8);
        if (endOfInput || (again[0] != 'n' && again[0] != 'N'))
            break;
        clockMin = (clockMin + 10) % (2 * DAY);
    }
}

/* ------------------------------------------------------------------ */
/*  Flight features                                                   */
/* ------------------------------------------------------------------ */
void viewAllFlights(void)
{
    int list[MAX_FLIGHTS], i;
    for (i = 0; i < flightCount; i++)
        list[i] = i;
    printFlightTable(list, flightCount);
}

void searchFlights(void)
{
    char query[64];
    int list[MAX_FLIGHTS], n = 0, i, d;

    readLine("  Search flight number / airline / destination: ", query, 64);
    if (query[0] == '\0') return;
    for (i = 0; i < flightCount; i++) {
        d = findAirport(flights[i].dest);
        if (containsIC(flights[i].number, query) || containsIC(flights[i].airline, query) ||
            containsIC(flights[i].dest, query) || containsIC(airports[d].city, query))
            list[n++] = i;
    }
    printFlightTable(list, n);
}

void findById(void)
{
    int id, pos;
    if (!readInt("  Enter flight ID: ", 1, 999999, &id)) return;
    pos = binarySearch(0, flightCount - 1, id);
    if (pos < 0) printf("  [!] No flight with ID %d.\n", id);
    else         showFlightDetail(pos);
}

/* compares two flights; key 1 = departure, 2 = airline, 3 = status */
int compareFlights(int x, int y, int key)
{
    int r;
    if (key == 1)      r = actualDep(x) - actualDep(y);
    else if (key == 2) r = strcmp(flights[x].airline, flights[y].airline);
    else               r = getStatus(x, clockMin) - getStatus(y, clockMin);
    if (r == 0)
        r = flights[x].id - flights[y].id;
    return r;
}

void sortAndFilter(void)
{
    int choice, list[MAX_FLIGHTS], n = 0, i, j, temp;
    char text[64];

    printf("\n  1. Sort by departure time\n  2. Sort by airline\n  3. Sort by status\n");
    printf("  4. Filter by airline\n  5. Filter by status\n");
    if (!readInt("  Choice: ", 1, 5, &choice)) return;

    if (choice <= 3) {
        for (i = 0; i < flightCount; i++)
            list[i] = i;
        /* bubble sort */
        for (i = 0; i < flightCount - 1; i++)
            for (j = 0; j < flightCount - 1 - i; j++)
                if (compareFlights(list[j], list[j + 1], choice) > 0) {
                    temp = list[j];
                    list[j] = list[j + 1];
                    list[j + 1] = temp;
                }
        printFlightTable(list, flightCount);
    } else if (choice == 4) {
        readLine("  Airline name (partial ok): ", text, 64);
        for (i = 0; i < flightCount; i++)
            if (containsIC(flights[i].airline, text))
                list[n++] = i;
        printFlightTable(list, n);
    } else {
        readLine("  Status (scheduled/delayed/airborne/landed/cancelled): ", text, 64);
        for (i = 0; i < flightCount; i++)
            if (equalIC(statusNames[getStatus(i, clockMin)], text))
                list[n++] = i;
        printFlightTable(list, n);
    }
}

void addFlight(void)
{
    struct Flight f;
    char text[64];
    int a, v;

    if (flightCount == MAX_FLIGHTS) { printf("  [!] Flight list is full.\n"); return; }
    memset(&f, 0, sizeof f);

    if (!readInt("  New flight ID: ", 1, 999999, &f.id)) return;
    if (idExists(f.id)) { printf("  [!] Flight ID %d already exists.\n", f.id); return; }

    readLine("  Flight number (e.g. PK305): ", f.number, 10);
    if (f.number[0] == '\0') { printf("  [!] Empty flight number.\n"); return; }
    toUpper(f.number);

    readLine("  Aircraft registration: ", f.reg, 12);
    toUpper(f.reg);
    a = findAircraft(f.reg);
    if (a < 0) { printf("  [!] Unknown aircraft (see Aircraft menu).\n"); return; }
    strcpy(f.airline, aircraft[a].airline);

    readLine("  Origin airport code: ", f.origin, 4);
    readLine("  Destination airport code: ", f.dest, 4);
    toUpper(f.origin);
    toUpper(f.dest);
    if (findAirport(f.origin) < 0 || findAirport(f.dest) < 0) { printf("  [!] Unknown airport code.\n"); return; }
    if (strcmp(f.origin, f.dest) == 0) { printf("  [!] Origin and destination are the same.\n"); return; }

    readLine("  Departure time UTC (HH:MM): ", text, 64);
    if (!parseTime(text, &f.depMin)) { printf("  [!] Invalid time.\n"); return; }
    if (!readInt("  Duration (minutes): ", 20, 1200, &f.durMin)) return;
    if (!readInt("  Delay (minutes, 0 if none): ", 0, 600, &f.delayMin)) return;
    if (!readInt("  Cancelled? (0 = no, 1 = yes): ", 0, 1, &v)) return;
    f.cancelled = v;
    if (!readInt("  Distance (km): ", 50, 20000, &f.distKm)) return;
    if (!readInt("  Cruise altitude (ft): ", 10000, 45000, &f.cruiseAlt)) return;

    flights[flightCount] = f;
    flightCount++;
    sortFlightsById();
    printf("  Flight %s added. (Use Save to write it to %s)\n", f.number, FLIGHT_FILE);
}

void trackFlight(void)
{
    char text[16];
    int i;
    readLine("  Flight number to track: ", text, 16);
    for (i = 0; i < flightCount; i++)
        if (equalIC(flights[i].number, text)) {
            showFlightDetail(i);
            return;
        }
    printf("  [!] Flight %s not found.\n", text);
}

void showActiveAndCompleted(void)
{
    int i, count = 0;
    char eta[16];

    printf("\n  >> ACTIVE FLIGHTS (airborne now)\n");
    printf("  %-7s %-9s %-16s %-10s %-9s %-8s %s\n",
           "FLIGHT", "ROUTE", "AIRLINE", "SPEED", "ALT(ft)", "ETA UTC", "PROGRESS");
    for (i = 0; i < flightCount; i++) {
        if (getStatus(i, clockMin) != AIRBORNE) continue;
        formatTime(eta, calcEta(i, clockMin));
        printf("  %-7s %s>%s    %-16.16s %-4d km/h  %-9d %-8s %.0f%%\n", flights[i].number,
               flights[i].origin, flights[i].dest, flights[i].airline,
               currentSpeed(i, clockMin), currentAltitude(i, clockMin), eta,
               flightProgress(i, clockMin) * 100);
        count++;
    }
    if (count == 0) printf("  (none)\n");

    printf("\n  >> COMPLETED FLIGHTS (landed)\n");
    printf("  %-7s %-9s %-16s %s\n", "FLIGHT", "ROUTE", "AIRLINE", "LANDED AT (UTC)");
    count = 0;
    for (i = 0; i < flightCount; i++) {
        if (getStatus(i, clockMin) != LANDED) continue;
        formatTime(eta, actualArr(i));
        printf("  %-7s %s>%s    %-16.16s %s\n", flights[i].number, flights[i].origin,
               flights[i].dest, flights[i].airline, eta);
        count++;
    }
    if (count == 0) printf("  (none)\n");
}

void showDelayed(void)
{
    int i, count = 0;
    char sched[16], now[16];

    printf("\n  %-7s %-16s %-9s %-10s %-10s %s\n",
           "FLIGHT", "AIRLINE", "ROUTE", "SCHED UTC", "NEW UTC", "DELAY");
    for (i = 0; i < flightCount; i++) {
        if (flights[i].delayMin <= 0 || flights[i].cancelled) continue;
        formatTime(sched, flights[i].depMin);
        formatTime(now, actualDep(i));
        printf("  %-7s %-16.16s %s>%s     %-10s %-10s %d min\n", flights[i].number,
               flights[i].airline, flights[i].origin, flights[i].dest, sched, now, flights[i].delayMin);
        count++;
    }
    if (count == 0) printf("  (no delayed flights)\n");
}

void showRoutes(void)
{
    int i, o, d;
    char t1[16], t2[16];

    printf("\n  %-7s %-34s %s\n", "FLIGHT", "DEPARTURE (airport, local time)", "ARRIVAL (airport, local time)");
    for (i = 0; i < flightCount; i++) {
        o = findAirport(flights[i].origin);
        d = findAirport(flights[i].dest);
        formatLocal(t1, actualDep(i), o);
        formatLocal(t2, actualArr(i), d);
        printf("  %-7s %s %-12s %-14s %s %s %s\n", flights[i].number,
               airports[o].code, airports[o].city, t1, airports[d].code, airports[d].city, t2);
    }
}

void showStatistics(void)
{
    int counts[5] = {0, 0, 0, 0, 0};
    int i, delayed = 0, totalDelay = 0;

    for (i = 0; i < flightCount; i++) {
        counts[getStatus(i, clockMin)]++;
        if (flights[i].delayMin > 0 && !flights[i].cancelled) {
            delayed++;
            totalDelay += flights[i].delayMin;
        }
    }
    printf("\n  ===== FLIGHT STATISTICS =====\n");
    printf("  Total flights     : %d\n", flightCount);
    printf("  Active (airborne) : %d\n", counts[AIRBORNE]);
    printf("  Scheduled         : %d\n", counts[SCHEDULED]);
    printf("  Delayed (waiting) : %d\n", counts[DELAYED]);
    printf("  Completed (landed): %d\n", counts[LANDED]);
    printf("  Cancelled         : %d\n", counts[CANCELLED]);
    printf("  Flights with delay: %d", delayed);
    if (delayed > 0)
        printf("  (average %d min)", totalDelay / delayed);
    printf("\n");
}

void showHistory(void)
{
    int list[MAX_FLIGHTS], n = 0, i, st;
    printf("\n  >> FLIGHT HISTORY (landed + cancelled)");
    for (i = 0; i < flightCount; i++) {
        st = getStatus(i, clockMin);
        if (st == LANDED || st == CANCELLED)
            list[n++] = i;
    }
    printFlightTable(list, n);
}

/* ------------------------------------------------------------------ */
/*  Aircraft features                                                 */
/* ------------------------------------------------------------------ */
void listAircraft(void)
{
    int i, j;
    printf("\n  %-9s %-18s %-16s %-9s %s\n", "REG", "MODEL", "AIRLINE", "SPEED", "ASSIGNED FLIGHT(S)");
    for (i = 0; i < aircraftCount; i++) {
        printf("  %-9s %-18.18s %-16.16s %-4d km/h ", aircraft[i].reg, aircraft[i].model,
               aircraft[i].airline, aircraft[i].speed);
        for (j = 0; j < flightCount; j++)
            if (equalIC(flights[j].reg, aircraft[i].reg))
                printf("%s ", flights[j].number);
        printf("\n");
    }
}

void searchAircraft(void)
{
    char query[32];
    int i, count = 0;

    readLine("  Search registration / model / airline: ", query, 32);
    if (query[0] == '\0') return;
    printf("\n  %-9s %-18s %-16s %s\n", "REG", "MODEL", "AIRLINE", "SPEED");
    for (i = 0; i < aircraftCount; i++)
        if (containsIC(aircraft[i].reg, query) || containsIC(aircraft[i].model, query) ||
            containsIC(aircraft[i].airline, query)) {
            printf("  %-9s %-18.18s %-16.16s %d km/h\n", aircraft[i].reg, aircraft[i].model,
                   aircraft[i].airline, aircraft[i].speed);
            count++;
        }
    if (count == 0) printf("  (no aircraft found)\n");
}

/* ------------------------------------------------------------------ */
/*  Time zone converter and simulation clock                          */
/* ------------------------------------------------------------------ */

/* finds the UTC offset of "UTC", "LOC" or an airport code; returns 1 if found */
int zoneOffset(const char name[], double *offset)
{
    int a;
    if (equalIC(name, "UTC")) { *offset = 0; return 1; }
    if (equalIC(name, "LOC") || equalIC(name, "LOCAL")) { *offset = LOCAL_UTC; return 1; }
    a = findAirport(name);
    if (a >= 0) { *offset = airports[a].utc; return 1; }
    return 0;
}

void timeZoneConverter(void)
{
    char from[16], to[16], text[16];
    double fromOff, toOff;
    int mins, utc, target, shift = 0;

    printf("\n  Zones: UTC, LOC (local, UTC+%.0f) or any airport code (e.g. DXB)\n", LOCAL_UTC);
    readLine("  Convert FROM zone: ", from, 16);
    if (!zoneOffset(from, &fromOff)) { printf("  [!] Unknown zone.\n"); return; }
    readLine("  Time there (HH:MM): ", text, 16);
    if (!parseTime(text, &mins)) { printf("  [!] Invalid time.\n"); return; }
    readLine("  Convert TO zone: ", to, 16);
    if (!zoneOffset(to, &toOff)) { printf("  [!] Unknown zone.\n"); return; }

    utc = mins - (int)(fromOff * 60);
    target = utc + (int)(toOff * 60);
    if (target < 0)          { target += DAY; shift = -1; }
    else if (target >= DAY)  { target -= DAY; shift = 1; }
    utc = (utc % DAY + DAY) % DAY;

    printf("\n  %s %s  =  UTC %02d:%02d  =  %s %02d:%02d", from, text, utc / 60, utc % 60,
           to, target / 60, target % 60);
    if (shift > 0) printf(" (next day)");
    if (shift < 0) printf(" (previous day)");
    printf("\n");
}

void simulationClock(void)
{
    int choice, value;
    char text[32], t[16];

    formatTime(t, clockMin);
    printf("\n  Simulation clock: %s UTC\n", t);
    printf("  1. Advance by N minutes\n  2. Set time (HH:MM UTC)\n");
    if (!readInt("  Choice: ", 1, 2, &choice)) return;

    if (choice == 1) {
        if (!readInt("  Minutes to advance: ", 1, DAY, &value)) return;
        clockMin = (clockMin + value) % (2 * DAY);
    } else {
        readLine("  New time (HH:MM): ", text, 32);
        if (!parseTime(text, &value)) { printf("  [!] Invalid time.\n"); return; }
        clockMin = value;
    }
    formatTime(t, clockMin);
    printf("  Clock is now %s UTC.\n", t);
}

/* ------------------------------------------------------------------ */
/*  Menus                                                             */
/* ------------------------------------------------------------------ */
void menuFlights(void)
{
    int choice;
    do {
        printf("\n --- FLIGHTS ---\n");
        printf("  1. View all flights\n");
        printf("  2. Search (flight no / airline / destination)\n");
        printf("  3. Find by flight ID (binary search)\n");
        printf("  4. Sort & filter\n");
        printf("  5. Add a flight\n");
        printf("  0. Back\n");
        if (!readInt(" Choice: ", 0, 5, &choice)) { choice = -1; continue; }
        switch (choice) {
            case 1: viewAllFlights(); break;
            case 2: searchFlights();  break;
            case 3: findById();       break;
            case 4: sortAndFilter();  break;
            case 5: addFlight();      break;
        }
        if (choice > 0) pressEnter();
    } while (choice != 0 && !endOfInput);
}

void menuAircraft(void)
{
    int choice;
    do {
        printf("\n --- AIRCRAFT ---\n");
        printf("  1. List all aircraft\n");
        printf("  2. Search aircraft\n");
        printf("  3. Speed & altitude of airborne flights\n");
        printf("  0. Back\n");
        if (!readInt(" Choice: ", 0, 3, &choice)) { choice = -1; continue; }
        switch (choice) {
            case 1: listAircraft();           break;
            case 2: searchAircraft();         break;
            case 3: showActiveAndCompleted(); break;
        }
        if (choice > 0) pressEnter();
    } while (choice != 0 && !endOfInput);
}

void menuTracking(void)
{
    int choice;
    do {
        printf("\n --- TRACKING ---\n");
        printf("  1. Airport search (arrivals / departures / on ground)\n");
        printf("  2. Track a flight (live details + ETA)\n");
        printf("  3. Active & completed flights\n");
        printf("  4. Delayed flights\n");
        printf("  5. Flight routes & airport timings\n");
        printf("  0. Back\n");
        if (!readInt(" Choice: ", 0, 5, &choice)) { choice = -1; continue; }
        switch (choice) {
            case 1: airportSearch();          break;
            case 2: trackFlight();            break;
            case 3: showActiveAndCompleted(); break;
            case 4: showDelayed();            break;
            case 5: showRoutes();             break;
        }
        if (choice > 1) pressEnter();
    } while (choice != 0 && !endOfInput);
}

void menuStatistics(void)
{
    int choice;
    do {
        printf("\n --- STATISTICS ---\n");
        printf("  1. Flight statistics\n");
        printf("  2. Flight history\n");
        printf("  3. Time zone converter\n");
        printf("  0. Back\n");
        if (!readInt(" Choice: ", 0, 3, &choice)) { choice = -1; continue; }
        switch (choice) {
            case 1: showStatistics();    break;
            case 2: showHistory();       break;
            case 3: timeZoneConverter(); break;
        }
        if (choice > 0) pressEnter();
    } while (choice != 0 && !endOfInput);
}

int main(void)
{
    int choice;
    char t[16];

    printf("=============================================\n");
    printf("        FLIGHT RADAR SYSTEM  (simulator)\n");
    printf("=============================================\n");
    loadAll();
    if (flightCount == 0) {
        printf("  No flight data loaded. Make sure the .txt files are in this folder.\n");
        return 1;
    }

    do {
        formatTime(t, clockMin);
        printf("\n=========== MAIN MENU  [sim time %s UTC] ===========\n", t);
        printf("  1. Flights\n  2. Aircraft\n  3. Tracking\n  4. Statistics\n");
        printf("  5. Simulation clock\n  6. Save flights to file\n  7. Reload data from files\n  0. Exit\n");
        if (!readInt(" Choice: ", 0, 7, &choice)) { choice = -1; continue; }
        switch (choice) {
            case 1: menuFlights();     break;
            case 2: menuAircraft();    break;
            case 3: menuTracking();    break;
            case 4: menuStatistics();  break;
            case 5: simulationClock(); break;
            case 6: saveFlights();     break;
            case 7: loadAll();         break;
        }
    } while (choice != 0 && !endOfInput);

    printf("\nGoodbye!\n");
    return 0;
}