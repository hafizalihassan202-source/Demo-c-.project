// ============================================================
//  GreenGrid - Smart Energy Billing and Load-Shedding System
//  A beginner-friendly C++ console program.
//
//  What it does:
//    1. Reads meter readings of campus buildings from a CSV file
//    2. Checks every row for errors
//    3. Calculates an electricity bill for each good building
//    4. When power is short, decides which buildings to switch
//       off (shed) using TWO different strategies and compares them
//    5. Tests its own functions using assert
//
//  Compile:  g++ greengrid.cpp -o greengrid
//  Run:      ./greengrid        (Windows: greengrid.exe)
// ============================================================

#include <iostream>   // cout, cin
#include <fstream>    // reading and writing files
#include <vector>     // vector (a list that can grow)
#include <string>     // string
#include <algorithm>  // sort
#include <cassert>    // assert (for testing)
#include <cmath>      // fabs (absolute value for decimals)
#include <iomanip>    // setw, setprecision (nice table printing)

using namespace std;

// ------------------------------------------------------------
//  CONSTANTS (values that never change while the program runs)
// ------------------------------------------------------------
const double RATE_SLAB1 = 10.0;    // Rs per unit for the first 100 units
const double RATE_SLAB2 = 15.0;    // Rs per unit for the next 200 units (101 to 300)
const double RATE_SLAB3 = 20.0;    // Rs per unit above 300 units
const double FIXED_CHARGE = 500.0; // Rs added to every bill
const double MAX_UNITS = 50000.0;  // more than this in one month is treated as a meter error

// ------------------------------------------------------------
//  STRUCTURES (our own "bundles" of data)
// ------------------------------------------------------------

// One building on campus.
struct Building {
    int id;                  // building number, example 1
    string name;             // example "Main Library"
    int priority;            // 1 = critical (keep power), 2 = normal, 3 = low (cut first)
    double previousReading;  // last month's meter reading
    double currentReading;   // this month's meter reading
    double units;            // units used = current - previous (also used as the building's load)
    double bill;             // bill in Rs
    bool valid;              // true if the row has no errors
    string error;            // describes the error if valid is false
};

// The result of running one load-shedding strategy.
struct SheddingResult {
    string strategyName;       // name to print
    vector<Building> shedList; // buildings that were switched off, in order
    double totalShed;          // total load switched off
    double neededShed;         // how much we really had to switch off
    int criticalShed;          // how many priority-1 buildings were switched off
    double extraShed;          // totalShed - neededShed (power cut that was not needed)
};

// ------------------------------------------------------------
//  SMALL HELPER FUNCTIONS
// ------------------------------------------------------------

// Returns true if two decimal numbers are (almost) equal.
// We never compare decimals with == because of tiny rounding errors.
bool almostEqual(double a, double b) {
    return fabs(a - b) < 0.001;
}

// Returns true if the character is a space, tab or line-ending.
bool isWhite(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Removes spaces from the start and end of a piece of text.
// Example: "  Library  " becomes "Library"
string trimSpaces(string text) {
    int start = 0;
    int end = (int)text.size() - 1;
    while (start <= end && isWhite(text[start])) {
        start++;
    }
    while (end >= start && isWhite(text[end])) {
        end--;
    }
    return text.substr(start, end - start + 1);
}

// Splits one CSV line into pieces at every comma.
// Example: "1,Library,1,100,250" becomes {"1","Library","1","100","250"}
vector<string> splitLine(string line) {
    vector<string> parts;
    string current = "";
    for (int i = 0; i < (int)line.size(); i++) {
        if (line[i] == ',') {
            parts.push_back(trimSpaces(current));
            current = "";
        } else {
            current = current + line[i];
        }
    }
    parts.push_back(trimSpaces(current)); // the last piece has no comma after it
    return parts;
}

// Returns true if the text is only digits, like "12" (no minus, no dot).
bool isWholeNumber(string text) {
    if (text.size() == 0 || text.size() > 9) {
        return false;
    }
    for (int i = 0; i < (int)text.size(); i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

// Returns true if the text is a number such as "12", "12.5" or "-3".
bool isNumber(string text) {
    if (text.size() == 0 || text.size() > 15) {
        return false;
    }
    int start = 0;
    if (text[0] == '-') {
        start = 1;
    }
    if (start == (int)text.size()) {
        return false; // the text was only "-"
    }
    int dots = 0;
    int digits = 0;
    for (int i = start; i < (int)text.size(); i++) {
        if (text[i] == '.') {
            dots++;
        } else if (text[i] >= '0' && text[i] <= '9') {
            digits++;
        } else {
            return false; // a letter or symbol was found
        }
    }
    return dots <= 1 && digits > 0;
}

// ------------------------------------------------------------
//  CHECKING A BUILDING FOR ERRORS
// ------------------------------------------------------------

// Works out the units and checks the building's data.
// Sets b.valid and b.error.
void validateBuilding(Building& b) {
    b.units = b.currentReading - b.previousReading;
    b.valid = true;
    b.error = "";

    if (b.name == "") {
        b.valid = false;
        b.error = "Building name is missing";
        return;
    }
    if (b.priority < 1 || b.priority > 3) {
        b.valid = false;
        b.error = "Priority must be 1, 2 or 3";
        return;
    }
    if (b.previousReading < 0 || b.currentReading < 0) {
        b.valid = false;
        b.error = "Meter reading is negative";
        return;
    }
    if (b.currentReading < b.previousReading) {
        b.valid = false;
        b.error = "Current reading is smaller than previous reading";
        return;
    }
    if (b.units > MAX_UNITS) {
        b.valid = false;
        b.error = "Units used are unrealistically high";
        return;
    }
}

// Builds a Building from values and checks it. (Used mostly in the tests.)
Building makeBuilding(int id, string name, int priority, double prev, double cur) {
    Building b;
    b.id = id;
    b.name = name;
    b.priority = priority;
    b.previousReading = prev;
    b.currentReading = cur;
    b.units = 0;
    b.bill = 0;
    b.valid = true;
    b.error = "";
    validateBuilding(b);
    return b;
}

// Turns one CSV line into a Building. If the line is broken, the building
// is returned with valid = false and a message in error.
Building parseRow(string line, int lineNumber) {
    vector<string> parts = splitLine(line);

    Building b;
    b.id = 0;
    b.name = "Line " + to_string(lineNumber);
    b.priority = 0;
    b.previousReading = 0;
    b.currentReading = 0;
    b.units = 0;
    b.bill = 0;
    b.valid = false;
    b.error = "";

    if (parts.size() != 5) {
        b.error = "Expected 5 columns but found " + to_string(parts.size());
        return b;
    }
    if (!isWholeNumber(parts[0])) {
        b.error = "Building ID is not a whole number";
        return b;
    }
    b.id = stoi(parts[0]);
    b.name = parts[1];

    if (!isWholeNumber(parts[2])) {
        b.error = "Priority is not a whole number";
        return b;
    }
    b.priority = stoi(parts[2]);

    if (!isNumber(parts[3])) {
        b.error = "Previous reading is not a number";
        return b;
    }
    if (!isNumber(parts[4])) {
        b.error = "Current reading is not a number";
        return b;
    }
    b.previousReading = stod(parts[3]);
    b.currentReading = stod(parts[4]);

    validateBuilding(b); // the logical checks
    return b;
}

// ------------------------------------------------------------
//  CSV FILES
// ------------------------------------------------------------

// Reads the meter-reading file into the vector.
// Returns false if the file could not be opened.
bool loadBuildings(string filename, vector<Building>& buildings) {
    ifstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }
    buildings.clear();

    string line;
    int lineNumber = 0;
    while (getline(file, line)) {
        lineNumber++;
        if (lineNumber == 1) {
            continue; // first line is the header, skip it
        }
        if (trimSpaces(line) == "") {
            continue; // skip empty lines
        }
        buildings.push_back(parseRow(line, lineNumber));
    }
    file.close();
    return true;
}

// Marks a building as invalid if an earlier valid building has the same ID.
void markDuplicateIds(vector<Building>& buildings) {
    for (int i = 0; i < (int)buildings.size(); i++) {
        for (int j = i + 1; j < (int)buildings.size(); j++) {
            if (buildings[i].valid && buildings[j].valid &&
                buildings[i].id == buildings[j].id) {
                buildings[j].valid = false;
                buildings[j].error = "Duplicate building ID";
            }
        }
    }
}

// Saves the bills to a CSV file. Returns false if the file cannot be created.
bool saveBills(string filename, const vector<Building>& buildings) {
    ofstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }
    file << "building_id,building_name,priority,units,bill,status\n";
    for (int i = 0; i < (int)buildings.size(); i++) {
        const Building& b = buildings[i];
        if (b.valid) {
            file << b.id << "," << b.name << "," << b.priority << ","
                 << b.units << "," << b.bill << ",OK\n";
        } else {
            file << b.id << "," << b.name << "," << b.priority << ",0,0,"
                 << "ERROR: " << b.error << "\n";
        }
    }
    file.close();
    return true;
}

// Saves what both strategies would switch off.
bool saveSheddingResults(string filename, const SheddingResult& a, const SheddingResult& b) {
    ofstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }
    file << "strategy,order,building_id,building_name,priority,units\n";
    for (int i = 0; i < (int)a.shedList.size(); i++) {
        file << a.strategyName << "," << (i + 1) << "," << a.shedList[i].id << ","
             << a.shedList[i].name << "," << a.shedList[i].priority << ","
             << a.shedList[i].units << "\n";
    }
    for (int i = 0; i < (int)b.shedList.size(); i++) {
        file << b.strategyName << "," << (i + 1) << "," << b.shedList[i].id << ","
             << b.shedList[i].name << "," << b.shedList[i].priority << ","
             << b.shedList[i].units << "\n";
    }
    file.close();
    return true;
}

// ------------------------------------------------------------
//  BILLING
// ------------------------------------------------------------

// Slab tariff:  first 100 units -> Rs 10, next 200 -> Rs 15, rest -> Rs 20,
// plus a fixed charge.
double calculateBill(double units) {
    double bill = FIXED_CHARGE;
    if (units <= 100) {
        bill = bill + units * RATE_SLAB1;
    } else if (units <= 300) {
        bill = bill + 100 * RATE_SLAB1 + (units - 100) * RATE_SLAB2;
    } else {
        bill = bill + 100 * RATE_SLAB1 + 200 * RATE_SLAB2 + (units - 300) * RATE_SLAB3;
    }
    return bill;
}

// Calculates the bill of every valid building.
void calculateAllBills(vector<Building>& buildings) {
    for (int i = 0; i < (int)buildings.size(); i++) {
        if (buildings[i].valid) {
            buildings[i].bill = calculateBill(buildings[i].units);
        }
    }
}

int countValid(const vector<Building>& buildings) {
    int count = 0;
    for (int i = 0; i < (int)buildings.size(); i++) {
        if (buildings[i].valid) {
            count++;
        }
    }
    return count;
}

// Adds up the load of all valid buildings.
double totalDemand(const vector<Building>& buildings) {
    double total = 0;
    for (int i = 0; i < (int)buildings.size(); i++) {
        if (buildings[i].valid) {
            total = total + buildings[i].units;
        }
    }
    return total;
}

// ------------------------------------------------------------
//  SORTING RULES FOR THE TWO STRATEGIES
//  sort() asks: "should a come BEFORE b?"  true = yes.
// ------------------------------------------------------------

// Strategy 1 (Priority First): least important buildings first (3, then 2, then 1).
// If priorities are equal, the bigger load goes first.
bool compareByPriority(const Building& a, const Building& b) {
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    return a.units > b.units;
}

// Strategy 2 (Biggest Load First): the biggest load goes first.
// If loads are equal, the less important building goes first.
bool compareByLoad(const Building& a, const Building& b) {
    if (a.units != b.units) {
        return a.units > b.units;
    }
    return a.priority > b.priority;
}

// ------------------------------------------------------------
//  LOAD SHEDDING
// ------------------------------------------------------------

// strategy = 1 -> Priority First,  strategy = 2 -> Biggest Load First.
// "buildings" is a COPY (no &), so sorting it does not change the caller's list.
SheddingResult runShedding(vector<Building> buildings, double available, int strategy) {
    SheddingResult result;
    if (strategy == 1) {
        result.strategyName = "Strategy 1: Priority First";
    } else {
        result.strategyName = "Strategy 2: Biggest Load First";
    }
    result.totalShed = 0;
    result.neededShed = 0;
    result.criticalShed = 0;
    result.extraShed = 0;

    // Only valid buildings that really use power can be shed.
    vector<Building> candidates;
    double demand = 0;
    for (int i = 0; i < (int)buildings.size(); i++) {
        if (buildings[i].valid && buildings[i].units > 0) {
            candidates.push_back(buildings[i]);
            demand = demand + buildings[i].units;
        }
    }

    double needed = demand - available;
    if (needed <= 0) {
        return result; // enough power, nothing to switch off
    }
    result.neededShed = needed;

    if (strategy == 1) {
        sort(candidates.begin(), candidates.end(), compareByPriority);
    } else {
        sort(candidates.begin(), candidates.end(), compareByLoad);
    }

    // Switch buildings off one by one until enough load is removed.
    for (int i = 0; i < (int)candidates.size(); i++) {
        if (result.totalShed >= needed) {
            break;
        }
        result.shedList.push_back(candidates[i]);
        result.totalShed = result.totalShed + candidates[i].units;
        if (candidates[i].priority == 1) {
            result.criticalShed++;
        }
    }
    result.extraShed = result.totalShed - needed;
    return result;
}

// Decides which result is better. Returns 1, 2, or 0 for a tie.
// Rule 1: fewer critical buildings switched off.
// Rule 2: fewer buildings switched off.
// Rule 3: less extra power cut.
int chooseBetter(const SheddingResult& a, const SheddingResult& b) {
    if (a.criticalShed != b.criticalShed) {
        if (a.criticalShed < b.criticalShed) return 1;
        return 2;
    }
    if (a.shedList.size() != b.shedList.size()) {
        if (a.shedList.size() < b.shedList.size()) return 1;
        return 2;
    }
    if (!almostEqual(a.extraShed, b.extraShed)) {
        if (a.extraShed < b.extraShed) return 1;
        return 2;
    }
    return 0;
}

// ------------------------------------------------------------
//  PRINTING
// ------------------------------------------------------------

void printBills(const vector<Building>& buildings) {
    cout << "\n--- BILLS (valid buildings) ---\n";
    cout << left << setw(5) << "ID" << setw(22) << "Name" << setw(10) << "Priority"
         << setw(10) << "Units" << setw(12) << "Bill (Rs)" << "\n";
    cout << string(59, '-') << "\n";
    double totalBill = 0;
    for (int i = 0; i < (int)buildings.size(); i++) {
        if (buildings[i].valid) {
            cout << left << setw(5) << buildings[i].id << setw(22) << buildings[i].name
                 << setw(10) << buildings[i].priority << setw(10) << fixed << setprecision(1)
                 << buildings[i].units << setw(12) << setprecision(2) << buildings[i].bill << "\n";
            totalBill = totalBill + buildings[i].bill;
        }
    }
    cout << string(59, '-') << "\n";
    cout << "Total of all bills: Rs " << fixed << setprecision(2) << totalBill << "\n";

    int errorCount = (int)buildings.size() - countValid(buildings);
    if (errorCount > 0) {
        cout << "\n--- ROWS WITH ERRORS (" << errorCount << ") ---\n";
        for (int i = 0; i < (int)buildings.size(); i++) {
            if (!buildings[i].valid) {
                string shownName = buildings[i].name;
                if (shownName == "") {
                    shownName = "(no name)";
                }
                cout << "ID " << buildings[i].id << " | " << shownName
                     << " | " << buildings[i].error << "\n";
            }
        }
    }
}

void printResult(const SheddingResult& r) {
    cout << "\n" << r.strategyName << "\n";
    if (r.shedList.size() == 0) {
        cout << "  No buildings need to be switched off.\n";
        return;
    }
    for (int i = 0; i < (int)r.shedList.size(); i++) {
        cout << "  " << (i + 1) << ". " << r.shedList[i].name
             << " (priority " << r.shedList[i].priority << ", "
             << fixed << setprecision(1) << r.shedList[i].units << " units)\n";
    }
    cout << "  Buildings switched off : " << r.shedList.size() << "\n";
    cout << "  Total load removed     : " << fixed << setprecision(1) << r.totalShed << "\n";
    cout << "  Load that had to go    : " << r.neededShed << "\n";
    cout << "  Extra (wasted) cut     : " << r.extraShed << "\n";
    cout << "  Critical buildings hit : " << r.criticalShed << "\n";
}

void printComparison(const SheddingResult& a, const SheddingResult& b) {
    cout << "\n=========== COMPARISON ===========\n";
    cout << left << setw(26) << "" << setw(12) << "Strategy 1" << setw(12) << "Strategy 2" << "\n";
    cout << setw(26) << "Buildings switched off" << setw(12) << a.shedList.size()
         << setw(12) << b.shedList.size() << "\n";
    cout << setw(26) << "Critical buildings hit" << setw(12) << a.criticalShed
         << setw(12) << b.criticalShed << "\n";
    cout << setw(26) << "Total load removed" << setw(12) << fixed << setprecision(1) << a.totalShed
         << setw(12) << b.totalShed << "\n";
    cout << setw(26) << "Extra (wasted) cut" << setw(12) << a.extraShed
         << setw(12) << b.extraShed << "\n";

    int better = chooseBetter(a, b);
    if (better == 1) {
        cout << "\nBetter choice: Strategy 1 (Priority First)\n";
    } else if (better == 2) {
        cout << "\nBetter choice: Strategy 2 (Biggest Load First)\n";
    } else {
        cout << "\nBoth strategies perform the same here.\n";
    }
    cout << "(Rule: fewer critical buildings hit, then fewer buildings, then less wasted cut.)\n";
}

// ------------------------------------------------------------
//  READING FROM THE KEYBOARD (safely)
// ------------------------------------------------------------

// Reads one full line of text from the user.
string readLine(string prompt) {
    cout << prompt;
    string text;
    if (!getline(cin, text)) {
        return "0"; // input has ended; return "0" so loops do not run forever
    }
    return trimSpaces(text);
}

// Keeps asking until the user types a number that is 0 or more.
double readPositiveNumber(string prompt) {
    while (true) {
        string text = readLine(prompt);
        if (isNumber(text) && stod(text) >= 0) {
            return stod(text);
        }
        cout << "Please type a number that is 0 or more.\n";
    }
}

// ------------------------------------------------------------
//  TESTS (assert stops the program with a message if a test fails)
// ------------------------------------------------------------

void testSplitLine() {
    vector<string> p = splitLine("1,Library,1,100,250");
    assert(p.size() == 5);
    assert(p[1] == "Library");
    assert(p[4] == "250");

    vector<string> q = splitLine("a,,c");  // empty middle piece
    assert(q.size() == 3);
    assert(q[1] == "");

    vector<string> r = splitLine(" x , y ");  // spaces are trimmed
    assert(r[0] == "x");
    assert(r[1] == "y");
}

void testNumberChecks() {
    assert(isNumber("12"));
    assert(isNumber("12.5"));
    assert(isNumber("-3"));
    assert(!isNumber("abc"));
    assert(!isNumber(""));
    assert(!isNumber("1.2.3"));
    assert(!isNumber("-"));
    assert(isWholeNumber("42"));
    assert(!isWholeNumber("4.2"));
    assert(!isWholeNumber("-4"));
}

void testCalculateBill() {
    assert(almostEqual(calculateBill(0), 500));      // only the fixed charge
    assert(almostEqual(calculateBill(50), 1000));    // 500 + 50*10
    assert(almostEqual(calculateBill(100), 1500));   // 500 + 100*10
    assert(almostEqual(calculateBill(200), 3000));   // 500 + 1000 + 100*15
    assert(almostEqual(calculateBill(400), 6500));   // 500 + 1000 + 3000 + 100*20
}

void testValidateBuilding() {
    Building good = makeBuilding(1, "Library", 1, 100, 250);
    assert(good.valid);
    assert(almostEqual(good.units, 150));

    assert(!makeBuilding(2, "Lab", 1, 500, 400).valid);   // reading went backwards
    assert(!makeBuilding(3, "Hall", 5, 100, 200).valid);  // bad priority
    assert(!makeBuilding(4, "", 2, 100, 200).valid);      // no name
    assert(!makeBuilding(5, "Shed", 2, -10, 50).valid);   // negative reading
    assert(!makeBuilding(6, "Mill", 2, 0, 99999).valid);  // too many units
}

void testParseRow() {
    assert(parseRow("1,Library,1,100,250", 2).valid);
    assert(!parseRow("1,Library,1,100", 2).valid);         // missing column
    assert(!parseRow("1,Library,1,100,abc", 2).valid);     // letters in a number
    assert(!parseRow("x,Library,1,100,250", 2).valid);     // bad ID
}

void testSortingRules() {
    vector<Building> list;
    list.push_back(makeBuilding(1, "A", 1, 0, 100));  // critical, 100 units
    list.push_back(makeBuilding(2, "B", 3, 0, 50));   // low, 50 units
    list.push_back(makeBuilding(3, "C", 2, 0, 200));  // normal, 200 units

    sort(list.begin(), list.end(), compareByPriority);
    assert(list[0].id == 2);  // priority 3 first
    assert(list[1].id == 3);
    assert(list[2].id == 1);  // critical last

    sort(list.begin(), list.end(), compareByLoad);
    assert(list[0].id == 3);  // 200 units first
    assert(list[1].id == 1);  // 100 units
    assert(list[2].id == 2);  // 50 units
}

void testShedding() {
    vector<Building> list;
    list.push_back(makeBuilding(1, "A", 1, 0, 100));
    list.push_back(makeBuilding(2, "B", 3, 0, 50));
    list.push_back(makeBuilding(3, "C", 2, 0, 200));
    // total = 350

    // Enough power: nothing is shed.
    SheddingResult none = runShedding(list, 400, 1);
    assert(none.shedList.size() == 0);

    // Available 280 -> must remove 70.
    SheddingResult s1 = runShedding(list, 280, 1);
    assert(s1.shedList.size() == 2);      // B (50) is not enough, so C (200) goes too
    assert(almostEqual(s1.totalShed, 250));
    assert(s1.criticalShed == 0);

    SheddingResult s2 = runShedding(list, 280, 2);
    assert(s2.shedList.size() == 1);      // C (200) alone is enough
    assert(almostEqual(s2.totalShed, 200));
    assert(chooseBetter(s1, s2) == 2);    // strategy 2 hits fewer buildings

    // No power at all: everything is shed.
    SheddingResult all = runShedding(list, 0, 1);
    assert(all.shedList.size() == 3);
    assert(all.criticalShed == 1);

    // The original list must not have been changed by sorting.
    assert(list[0].id == 1);
}

void testCriticalProtection() {
    vector<Building> list;
    list.push_back(makeBuilding(1, "Lab", 1, 0, 500));   // critical, big
    list.push_back(makeBuilding(2, "Cafe", 3, 0, 100));
    list.push_back(makeBuilding(3, "Shop", 3, 0, 100));
    // total = 700, available 500 -> must remove 200

    SheddingResult s1 = runShedding(list, 500, 1);
    SheddingResult s2 = runShedding(list, 500, 2);
    assert(s1.criticalShed == 0);   // Cafe + Shop = 200, critical lab is safe
    assert(s1.shedList.size() == 2);
    assert(s2.criticalShed == 1);   // biggest load is the critical lab
    assert(chooseBetter(s1, s2) == 1);
}

void runAllTests() {
    testSplitLine();
    testNumberChecks();
    testCalculateBill();
    testValidateBuilding();
    testParseRow();
    testSortingRules();
    testShedding();
    testCriticalProtection();
    cout << "All tests passed.\n";
}

// ------------------------------------------------------------
//  MAIN PROGRAM
// ------------------------------------------------------------

void printMenu() {
    cout << "\n===== GreenGrid Menu =====\n";
    cout << "1. Load meter readings from a CSV file\n";
    cout << "2. Show bills\n";
    cout << "3. Save bills to bills.csv\n";
    cout << "4. Run load-shedding with ONE strategy\n";
    cout << "5. Compare both strategies\n";
    cout << "0. Exit\n";
}

int main() {
    runAllTests(); // if any assert fails, the program stops right here

    vector<Building> buildings;
    bool dataLoaded = false;

    while (true) {
        printMenu();
        string choice = readLine("Your choice: ");

        if (choice == "0") {
            cout << "Goodbye!\n";
            break;

        } else if (choice == "1") {
            string filename = readLine("File name (press Enter for readings.csv): ");
            if (filename == "") {
                filename = "readings.csv";
            }
            if (!loadBuildings(filename, buildings)) {
                cout << "Could not open " << filename << "\n";
            } else {
                markDuplicateIds(buildings);
                calculateAllBills(buildings);
                dataLoaded = true;
                cout << "Loaded " << buildings.size() << " rows: "
                     << countValid(buildings) << " valid, "
                     << (buildings.size() - countValid(buildings)) << " with errors.\n";
            }

        } else if (choice == "2") {
            if (!dataLoaded) {
                cout << "Please load a file first (option 1).\n";
            } else {
                printBills(buildings);
            }

        } else if (choice == "3") {
            if (!dataLoaded) {
                cout << "Please load a file first (option 1).\n";
            } else if (saveBills("bills.csv", buildings)) {
                cout << "Saved to bills.csv\n";
            } else {
                cout << "Could not write bills.csv\n";
            }

        } else if (choice == "4") {
            if (!dataLoaded) {
                cout << "Please load a file first (option 1).\n";
            } else {
                cout << "Total demand of valid buildings: " << fixed << setprecision(1)
                     << totalDemand(buildings) << " units\n";
                string which = readLine("Strategy (1 = Priority First, 2 = Biggest Load First): ");
                if (which != "1" && which != "2") {
                    cout << "Please type 1 or 2.\n";
                } else {
                    double available = readPositiveNumber("Power available (units): ");
                    SheddingResult r = runShedding(buildings, available, stoi(which));
                    printResult(r);
                }
            }

        } else if (choice == "5") {
            if (!dataLoaded) {
                cout << "Please load a file first (option 1).\n";
            } else {
                cout << "Total demand of valid buildings: " << fixed << setprecision(1)
                     << totalDemand(buildings) << " units\n";
                double available = readPositiveNumber("Power available (units): ");
                SheddingResult a = runShedding(buildings, available, 1);
                SheddingResult b = runShedding(buildings, available, 2);
                printResult(a);
                printResult(b);
                printComparison(a, b);
                if (saveSheddingResults("shedding_results.csv", a, b)) {
                    cout << "\nDetails saved to shedding_results.csv\n";
                }
            }

        } else {
            cout << "Invalid choice. Please type 0, 1, 2, 3, 4 or 5.\n";
        }
    }
    return 0;
}
