/*
 * ================================================================
 *  Rubik's Cube Solver -- Enhanced IDA* (Korf-style)
 *  Compatible: GCC 6.3+ MinGW/Windows, GCC/Clang Linux/Mac
 *  Compile:  g++ -O3 -std=c++14 -o solver rubiks_solver_single.cpp
 *  Run:      .\solver.exe   (Windows)   ./solver  (Linux/Mac)
 * ================================================================
 *  Pattern databases (flat arrays, no hash maps, no external libs):
 *   corner_db_part1.bin .. corner_db_part5.bin   (20 MB each, ~84 MB total)
 *   edge6a_db_part1.bin, edge6a_db_part2.bin    (~21 MB each, ~40 MB total)
 *   edge6b_db_part1.bin, edge6b_db_part2.bin    (~21 MB each, ~40 MB total)
 *  Assembled in RAM from parts via plain fread -- zero dependencies.
 *  h(n) = max(corner_h, edge4_h)  -- admissible lower bound
 *  IDA* with same-face + opposite-face canonical pruning
 * ================================================================
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <queue>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <chrono>
#include <climits>
#include <algorithm>
#include <cstdio>
using namespace std;

// Move = face*3 + type
// face: U=0 D=1 L=2 R=3 F=4 B=5    type: CW=0  180=1  CCW=2
static const char* MN[18] = {
    "U","U2","U'",  "D","D2","D'",
    "L","L2","L'",  "R","R2","R'",
    "F","F2","F'",  "B","B2","B'"
};

// ================================================================
//  Cube  --  s[face][sticker], 6 faces x 9 stickers
//  Sticker layout per face (viewed from outside):
//    0 1 2
//    3 4 5
//    6 7 8
//  Solved color of face f = f  (W=0 O=1 G=2 R=3 B=4 Y=5)
// ================================================================
struct Cube {
    uint8_t s[6][9];

    Cube() { reset(); }
    void reset() {
        for (int f = 0; f < 6; f++)
            for (int i = 0; i < 9; i++)
                s[f][i] = (uint8_t)f;
    }
    bool solved() const {
        for (int f = 0; f < 6; f++)
            for (int i = 1; i < 9; i++)
                if (s[f][i] != s[f][0]) return false;
        return true;
    }

    void rcw(int f) {                       // rotate face f clockwise
        uint8_t t[9]; memcpy(t, s[f], 9);
        s[f][0]=t[6]; s[f][1]=t[3]; s[f][2]=t[0];
        s[f][3]=t[7];               s[f][5]=t[1];
        s[f][6]=t[8]; s[f][7]=t[5]; s[f][8]=t[2];
    }
    inline void cyc(int fa,int ia,int fb,int ib,int fc,int ic,int fd,int id) {
        uint8_t t=s[fa][ia];
        s[fa][ia]=s[fd][id]; s[fd][id]=s[fc][ic];
        s[fc][ic]=s[fb][ib]; s[fb][ib]=t;
    }

    void U1(){rcw(0);cyc(4,0,3,0,2,0,1,0);cyc(4,1,3,1,2,1,1,1);cyc(4,2,3,2,2,2,1,2);}
    void D1(){rcw(5);cyc(2,6,3,6,4,6,1,6);cyc(2,7,3,7,4,7,1,7);cyc(2,8,3,8,4,8,1,8);}
    void L1(){rcw(1);cyc(0,0,2,0,5,0,4,8);cyc(0,3,2,3,5,3,4,5);cyc(0,6,2,6,5,6,4,2);}
    void R1(){rcw(3);cyc(0,2,4,6,5,2,2,2);cyc(0,5,4,3,5,5,2,5);cyc(0,8,4,0,5,8,2,8);}
    void F1(){rcw(2);cyc(0,6,3,0,5,2,1,8);cyc(0,7,3,3,5,1,1,5);cyc(0,8,3,6,5,0,1,2);}
    void B1(){rcw(4);cyc(0,0,1,6,5,8,3,2);cyc(0,1,1,3,5,7,3,5);cyc(0,2,1,0,5,6,3,8);}

    void apply(int m) {
        int face = m/3, type = m%3;
        int n = (type==0) ? 1 : (type==1) ? 2 : 3;
        for (int i = 0; i < n; i++)
            switch(face){
                case 0:U1();break; case 1:D1();break;
                case 2:L1();break; case 3:R1();break;
                case 4:F1();break; case 5:B1();break;
            }
    }

    void print() const {
        static const char C[6] = {'W','O','G','R','B','Y'};
        cout << "\n";
        cout << "            +-------+\n";
        for (int r=0;r<3;r++){
            cout << "            | ";
            for (int i=0;i<3;i++) cout << C[s[0][r*3+i]] << " ";
            cout << "|\n";
        }
        cout << "+-------+-------+-------+-------+\n";
        for (int r=0;r<3;r++){
            int belt[4]={1,2,3,4};
            for (int fi=0;fi<4;fi++){
                cout << "| ";
                for (int i=0;i<3;i++) cout << C[s[belt[fi]][r*3+i]] << " ";
            }
            cout << "|\n";
        }
        cout << "+-------+-------+-------+-------+\n";
        cout << "            +-------+\n";
        for (int r=0;r<3;r++){
            cout << "            | ";
            for (int i=0;i<3;i++) cout << C[s[5][r*3+i]] << " ";
            cout << "|\n";
        }
        cout << "            +-------+\n";
        cout << "  [U=top] [L | F | R | B = belt] [D=bottom]\n";
        cout << "  W=White O=Orange G=Green R=Red B=Blue Y=Yellow\n\n";
    }
};

// ================================================================
//  Corner Pattern Database
//  Tracks all 8 corner pieces: position (0-7) + orientation (0-2)
//  Index = Lehmer_rank(perm) * 2187 + orientation_rank
//  Size  = 8! * 3^7 = 88,179,840  (flat uint8_t array, 85 MB)
// ================================================================

// CF[slot][sticker 0-2] = {face, sticker_index}
// Slots: URF UFL ULB UBR DFR DFL DBL DRB
static const int CF[8][3][2] = {
    {{0,8},{3,0},{2,2}}, {{0,6},{2,0},{1,2}},
    {{0,0},{1,0},{4,2}}, {{0,2},{4,0},{3,2}},
    {{5,2},{2,8},{3,6}}, {{5,0},{1,8},{2,6}},
    {{5,6},{4,8},{1,6}}, {{5,8},{3,8},{4,6}}
};
static const int SC[8][3] = {         // solved colors per slot
    {0,3,2},{0,2,1},{0,1,4},{0,4,3},
    {5,2,3},{5,1,2},{5,4,1},{5,3,4}
};
static const int FACT8[8] = {5040,720,120,24,6,2,1,1};
static const int POW3[8]  = {1,3,9,27,81,243,729,2187};
static const int CORNER_SIZE = 88179840;

static int cornerIdx(const Cube& c) {
    uint8_t perm[8], ori[8];
    for (int sl=0; sl<8; sl++) {
        int fa=c.s[CF[sl][0][0]][CF[sl][0][1]];
        int fb=c.s[CF[sl][1][0]][CF[sl][1][1]];
        int fc=c.s[CF[sl][2][0]][CF[sl][2][1]];
        perm[sl]=0; ori[sl]=0;
        for (int p=0; p<8; p++) {
            if(fa==SC[p][0]&&fb==SC[p][1]&&fc==SC[p][2]){perm[sl]=p;ori[sl]=0;goto dc;}
            if(fa==SC[p][1]&&fb==SC[p][2]&&fc==SC[p][0]){perm[sl]=p;ori[sl]=1;goto dc;}
            if(fa==SC[p][2]&&fb==SC[p][0]&&fc==SC[p][1]){perm[sl]=p;ori[sl]=2;goto dc;}
        }
        dc:;
    }
    int pr=0; bool used[8]={};
    for (int i=0; i<8; i++) {
        int cnt=0;
        for (int j=0; j<(int)perm[i]; j++) if(!used[j]) cnt++;
        used[perm[i]]=true;
        pr += cnt * FACT8[i];
    }
    int or_=0;
    for (int i=0; i<7; i++) or_ += ori[i]*POW3[i];
    return pr*2187 + or_;
}

// ================================================================
//  Edge-6 Pattern Databases  (TWO complementary sets)
//  edge6a: UF(0) UR(1) UB(2) UL(3) DF(8) DR(9)  -- top+bottom edges
//  edge6b: FR(4) FL(5) BL(6) BR(7) DB(10) DL(11) -- belt+bottom edges
//  Together they cover all 12 edges.
//  h = max(corner, edge6a, edge6b) -- very tight lower bound
//  Each DB size = P(12,6) * 2^6 = 42,577,920  (~40 MB)
// ================================================================

// Edge facelets EF[slot][sticker]{face,idx}
// 12 edges: UF=0 UR=1 UB=2 UL=3 FR=4 FL=5 BL=6 BR=7 DF=8 DR=9 DB=10 DL=11
static const int EF[12][2][2] = {
    {{0,7},{2,1}}, {{0,5},{3,1}}, {{0,1},{4,1}}, {{0,3},{1,1}},
    {{2,5},{3,3}}, {{2,3},{1,5}}, {{4,5},{1,3}}, {{4,3},{3,5}},
    {{5,1},{2,7}}, {{5,5},{3,7}}, {{5,7},{4,7}}, {{5,3},{1,7}}
};
static const int SE[12][2] = {
    {0,2},{0,3},{0,4},{0,1},{2,3},{2,1},{4,1},{4,3},{5,2},{5,3},{5,4},{5,1}
};

static const int TRACKED6A[6] = {0, 1, 2, 3, 8, 9};   // UF UR UB UL DF DR
static const int TRACKED6B[6] = {4, 5, 6, 7, 10, 11};  // FR FL BL BR DB DL
static const int EDGE6_SIZE   = 42577920; // P(12,6) * 2^6

static int partialLehmer6(int p[6]) {
    int rank = 0;
    bool used[12] = {};
    int base = 12;
    for (int i = 0; i < 6; i++) {
        int cnt = 0;
        for (int j = 0; j < p[i]; j++) if (!used[j]) cnt++;
        used[p[i]] = true;
        base--;
        int rem = 1;
        for (int k = 0; k < (5-i); k++) rem *= (base - k);
        rank += cnt * rem;
    }
    return rank;
}

// Generic edge-6 index using any set of 6 tracked slots
static int edge6IdxGeneric(const Cube& c, const int tracked[6]) {
    int perm[6], flip[6];
    for (int i = 0; i < 6; i++) {
        int sl = tracked[i];
        int fa = c.s[EF[sl][0][0]][EF[sl][0][1]];
        int fb = c.s[EF[sl][1][0]][EF[sl][1][1]];
        perm[i] = 0; flip[i] = 0;
        for (int e = 0; e < 12; e++) {
            if (fa==SE[e][0]&&fb==SE[e][1]) { perm[i]=e; flip[i]=0; goto de; }
            if (fa==SE[e][1]&&fb==SE[e][0]) { perm[i]=e; flip[i]=1; goto de; }
        }
        de:;
    }
    int pr = partialLehmer6(perm);
    int fr = flip[0]*32 + flip[1]*16 + flip[2]*8 + flip[3]*4 + flip[4]*2 + flip[5];
    return pr * 64 + fr;
}

static int edge6aIdx(const Cube& c) { return edge6IdxGeneric(c, TRACKED6A); }
static int edge6bIdx(const Cube& c) { return edge6IdxGeneric(c, TRACKED6B); }

// ================================================================
//  PatternDB -- three flat uint8_t arrays, no hash maps
// ================================================================
class PatternDB {
    vector<uint8_t> cdb;    // corner  DB  (85 MB)
    vector<uint8_t> edba;   // edge-6a DB  (40 MB)
    vector<uint8_t> edbb;   // edge-6b DB  (40 MB)

public:
    PatternDB() {
        cout << "  Allocating databases (85+40+40 MB)..." << flush;
        cdb.assign(CORNER_SIZE, 255);
        edba.assign(EDGE6_SIZE,  255);
        edbb.assign(EDGE6_SIZE,  255);
        cout << " done.\n" << flush;
    }

    // ── Load from disk ────────────────────────────────────────
    bool load() {
        bool cok = false, eaok = false, ebok = false;

        // Corner DB (split parts)
        cout << "  Loading corner DB (5 parts)...\n" << flush;
        {
            size_t total = 0; bool allOk = true;
            for (int p = 1; p <= 5; p++) {
                char fname[64]; sprintf(fname, "corner_db_part%d.bin", p);
                cout << "    Part " << p << ": " << fname << "..." << flush;
                FILE* f = fopen(fname, "rb");
                if (!f) { cout << " NOT FOUND\n" << flush; allOk=false; break; }
                size_t got = fread(cdb.data()+total, 1, (size_t)CORNER_SIZE-total, f);
                fclose(f); total += got;
                cout << " OK (" << got/1024/1024 << " MB)\n" << flush;
            }
            cok = (total==(size_t)CORNER_SIZE && allOk);
        }
        cout << (cok ? "  Corner DB OK\n" : "  Corner DB FAILED\n") << flush;

        // Edge-6a (split parts)
        cout << "  Loading edge6a DB (2 parts)...\n" << flush;
        {
            size_t total = 0; bool allOk = true;
            for (int p = 1; p <= 2; p++) {
                char fname[64]; sprintf(fname, "edge6a_db_part%d.bin", p);
                cout << "    Part " << p << ": " << fname << "..." << flush;
                FILE* f = fopen(fname, "rb");
                if (!f) { cout << " NOT FOUND\n" << flush; allOk=false; break; }
                size_t got = fread(edba.data()+total, 1, (size_t)EDGE6_SIZE-total, f);
                fclose(f); total += got;
                cout << " OK (" << got/1024/1024 << " MB)\n" << flush;
            }
            eaok = (total==(size_t)EDGE6_SIZE && allOk);
        }
        cout << (eaok ? "  Edge6a DB OK\n" : "  Edge6a DB FAILED\n") << flush;

        // Edge-6b (split parts)
        cout << "  Loading edge6b DB (2 parts)...\n" << flush;
        {
            size_t total = 0; bool allOk = true;
            for (int p = 1; p <= 2; p++) {
                char fname[64]; sprintf(fname, "edge6b_db_part%d.bin", p);
                cout << "    Part " << p << ": " << fname << "..." << flush;
                FILE* f = fopen(fname, "rb");
                if (!f) { cout << " NOT FOUND\n" << flush; allOk=false; break; }
                size_t got = fread(edbb.data()+total, 1, (size_t)EDGE6_SIZE-total, f);
                fclose(f); total += got;
                cout << " OK (" << got/1024/1024 << " MB)\n" << flush;
            }
            ebok = (total==(size_t)EDGE6_SIZE && allOk);
        }
        cout << (ebok ? "  Edge6b DB OK\n" : "  Edge6b DB FAILED\n") << flush;

        if (cok && eaok && ebok) { cout << "  Ready!\n\n" << flush; return true; }
        return false;
    }

    // ── Save to disk ──────────────────────────────────────────
    void save() {
        // Corner parts
        const size_t PART = 20*1024*1024;
        size_t total = 0; int part = 1;
        cout << "  Saving corner DB (split parts)...\n" << flush;
        while (total < (size_t)CORNER_SIZE) {
            char fname[64]; sprintf(fname, "corner_db_part%d.bin", part);
            size_t chunk = min((size_t)CORNER_SIZE-total, PART);
            FILE* f = fopen(fname, "wb");
            if (f) { fwrite(cdb.data()+total,1,chunk,f); fclose(f);
                     cout << "    Saved " << fname << "\n" << flush; }
            total += chunk; part++;
        }
        // edge6a (split 2 parts)
        {
            const size_t EPART = 21*1024*1024; // 21 MB per part
            size_t etotal = 0; int epart = 1;
            cout << "  Saving edge6a DB (split parts)...\n" << flush;
            while (etotal < (size_t)EDGE6_SIZE) {
                char fname[64]; sprintf(fname, "edge6a_db_part%d.bin", epart);
                size_t chunk = min((size_t)EDGE6_SIZE-etotal, EPART);
                FILE* f = fopen(fname, "wb");
                if (f) { fwrite(edba.data()+etotal,1,chunk,f); fclose(f);
                         cout << "    Saved " << fname << "\n" << flush; }
                etotal += chunk; epart++;
            }
        }
        // edge6b (split 2 parts)
        {
            const size_t EPART = 21*1024*1024; // 21 MB per part
            size_t etotal = 0; int epart = 1;
            cout << "  Saving edge6b DB (split parts)...\n" << flush;
            while (etotal < (size_t)EDGE6_SIZE) {
                char fname[64]; sprintf(fname, "edge6b_db_part%d.bin", epart);
                size_t chunk = min((size_t)EDGE6_SIZE-etotal, EPART);
                FILE* f = fopen(fname, "wb");
                if (f) { fwrite(edbb.data()+etotal,1,chunk,f); fclose(f);
                         cout << "    Saved " << fname << "\n" << flush; }
                etotal += chunk; epart++;
            }
        }
        cout << "\n" << flush;
    }

    // ── BFS: build corner DB ──────────────────────────────────
    void buildCorner(int dep=9) {
        cout << "\n  [1/3] Building corner DB (depth " << dep << ")...\n";
        cout << "        ~73M states. Takes 2-4 min.\n\n" << flush;
        auto t0 = chrono::steady_clock::now();
        Cube solved; cdb[cornerIdx(solved)]=0;
        struct Node { Cube c; uint8_t d; };
        queue<Node> q; Node init; init.c=solved; init.d=0; q.push(init);
        size_t filled=1; uint8_t lastD=255;
        while (!q.empty()) {
            Node front=q.front(); q.pop();
            Cube cu=front.c; uint8_t d=front.d;
            if (d!=lastD) { lastD=d;
                int s=(int)chrono::duration<double>(chrono::steady_clock::now()-t0).count();
                cout<<"        Depth "<<(int)d<<"  states="<<filled<<"  time="<<s<<"s\n"<<flush; }
            if (d>=dep) continue;
            for (int m=0;m<18;m++) {
                Cube nx=cu; nx.apply(m); int r=cornerIdx(nx);
                if (cdb[r]==255) { cdb[r]=d+1; filled++;
                    if(d+1<dep){Node nd;nd.c=nx;nd.d=d+1;q.push(nd);} }
            }
        }
        int s=(int)chrono::duration<double>(chrono::steady_clock::now()-t0).count();
        cout<<"        Done: "<<filled<<" states in "<<s<<"s\n"<<flush;
    }

    // ── BFS: build one edge-6 DB ──────────────────────────────
    void buildEdge6(vector<uint8_t>& db, const int tracked[6],
                    const char* fname, int num, int dep=10) {
        cout << "\n  [" << num << "/3] Building " << fname
             << " (depth " << dep << ")...\n";
        cout << "        ~42M states. Takes 3-8 min.\n\n" << flush;
        auto t0 = chrono::steady_clock::now();
        Cube solved; db[edge6IdxGeneric(solved,tracked)]=0;
        struct Node { Cube c; uint8_t d; };
        queue<Node> q; Node init; init.c=solved; init.d=0; q.push(init);
        size_t filled=1; uint8_t lastD=255;
        while (!q.empty()) {
            Node front=q.front(); q.pop();
            Cube cu=front.c; uint8_t d=front.d;
            if (d!=lastD) { lastD=d;
                int s=(int)chrono::duration<double>(chrono::steady_clock::now()-t0).count();
                cout<<"        Depth "<<(int)d<<"  states="<<filled<<"  time="<<s<<"s\n"<<flush; }
            if (d>=dep) continue;
            for (int m=0;m<18;m++) {
                Cube nx=cu; nx.apply(m); int r=edge6IdxGeneric(nx,tracked);
                if (db[r]==255) { db[r]=d+1; filled++;
                    if(d+1<dep){Node nd;nd.c=nx;nd.d=d+1;q.push(nd);} }
            }
        }
        int s=(int)chrono::duration<double>(chrono::steady_clock::now()-t0).count();
        cout<<"        Done: "<<filled<<" states in "<<s<<"s\n"<<flush;
    }

    void build() {
        buildCorner(9);
        buildEdge6(edba, TRACKED6A, "edge6a_db.bin", 2, 10);
        buildEdge6(edbb, TRACKED6B, "edge6b_db.bin", 3, 10);
        save();
        cout << "  All databases ready!\n\n" << flush;
    }

    // ── Heuristic: max of all three lower bounds ──────────────
    uint8_t h(const Cube& c) const {
        uint8_t ch  = cdb[cornerIdx(c)];   if (ch==255) ch=0;
        uint8_t eah = edba[edge6aIdx(c)];  if (eah==255) eah=0;
        uint8_t ebh = edbb[edge6bIdx(c)];  if (ebh==255) ebh=0;
        uint8_t r = ch > eah ? ch : eah;
        return r > ebh ? r : ebh;
    }
};

// ================================================================
//  IDA* Solver
// ================================================================
static const PatternDB* gDB    = nullptr;
static vector<int>      gSol;
static long long        gNodes = 0;
static bool             gFound = false;

void dfs(Cube& cube, int g, int bound, vector<int>& path, int lf, int& nb) {
    if (gFound) return;
    int h = (int)gDB->h(cube);
    int f = g + h;
    if (f > bound) { if (f < nb) nb = f; return; }
    if (cube.solved()) { gSol = path; gFound = true; return; }
    gNodes++;
    for (int m = 0; m < 18; m++) {
        int cf = m/3;
        if (cf == lf) continue;                          // same face: skip
        if (lf>=0 && lf/2==cf/2 && cf<lf) continue;    // opposite canonical
        Cube nx = cube; nx.apply(m);
        path.push_back(m);
        dfs(nx, g+1, bound, path, cf, nb);
        path.pop_back();
        if (gFound) return;
    }
}

vector<int> solveWith(const Cube& cube, PatternDB& db, int maxDep = 25) {
    gDB = &db;
    gNodes = 0; gSol.clear(); gFound = false;
    if (cube.solved()) return vector<int>();

    int bound = (int)db.h(cube);
    vector<int> path;
    auto t0 = chrono::steady_clock::now();

    cout << "  IDA* searching (max " << maxDep << " moves)...\n" << flush;
    while (bound <= maxDep && !gFound) {
        path.clear();
        int nb = INT_MAX;
        cout << "  Depth " << bound << "..." << flush;
        dfs(const_cast<Cube&>(cube), 0, bound, path, -1, nb);
        double ms = chrono::duration<double,milli>(
            chrono::steady_clock::now()-t0).count();
        cout << " " << gNodes << " nodes, " << (int)ms << "ms\n" << flush;
        if (gFound) {
            cout << "\n  Solution: " << gSol.size() << " moves\n" << flush;
            return gSol;
        }
        if (nb == INT_MAX) break;
        bound = nb;
    }
    cout << "  No solution found within depth " << maxDep << ".\n" << flush;
    return vector<int>();
}

// ================================================================
//  Utilities
// ================================================================
vector<int> parseSeq(const string& s) {
    vector<int> mv;
    istringstream ss(s); string t;
    while (ss >> t) {
        int face = -1; char f0 = t[0];
        if      (f0=='U') face=0; else if (f0=='D') face=1;
        else if (f0=='L') face=2; else if (f0=='R') face=3;
        else if (f0=='F') face=4; else if (f0=='B') face=5;
        else { cout << "  Unknown move: " << t << "\n"; continue; }
        string mod = (t.size()>1) ? t.substr(1) : "";
        int tp = 0;
        if (mod=="2") tp=1; else if (mod=="'"||mod=="i") tp=2;
        mv.push_back(face*3 + tp);
    }
    return mv;
}

string randScramble(int n) {
    string r; int last = -1;
    for (int i = 0; i < n; i++) {
        int m, cf;
        do { m = rand()%18; cf = m/3; } while (cf == last);
        last = cf;
        if (!r.empty()) r += " ";
        r += MN[m];
    }
    return r;
}

void printSol(const vector<int>& mv) {
    if (mv.empty()) { cout << "(none)\n"; return; }
    for (int i = 0; i < (int)mv.size(); i++) {
        cout << (i+1) << "." << MN[mv[i]] << "  ";
        if ((i+1)%12 == 0) cout << "\n";
    }
    if (mv.size()%12 != 0) cout << "\n";
}

void runBench(PatternDB& db, int tests, int depth) {
    cout << "\n==========================================\n";
    cout << "  BENCHMARK: " << tests << " tests, scramble=" << depth << " moves\n";
    cout << "==========================================\n\n" << flush;
    int ok=0; double tms=0; long long tnodes=0; int mn=INT_MAX, mx=0;
    for (int t = 0; t < tests; t++) {
        string sc = randScramble(depth);
        Cube c;
        vector<int> smv = parseSeq(sc);
        for (int i=0;i<(int)smv.size();i++) c.apply(smv[i]);
        auto t0 = chrono::steady_clock::now();
        vector<int> sol = solveWith(c, db, 25);
        double ms = chrono::duration<double,milli>(
            chrono::steady_clock::now()-t0).count();
        if (!sol.empty() || c.solved()) {
            ok++; tms+=ms; tnodes+=gNodes;
            int len=(int)sol.size();
            if (len<mn) mn=len;
            if (len>mx) mx=len;
            cout << "  [" << t+1 << "/" << tests << "] " << sc
                 << "\n       -> " << len << " moves, " << (int)ms << "ms\n" << flush;
        } else {
            cout << "  [" << t+1 << "/" << tests << "] FAILED: " << sc << "\n" << flush;
        }
    }
    cout << "\n  Solved: " << ok << "/" << tests;
    if (ok>0)
        cout << "  avg=" << (int)(tms/ok) << "ms"
             << "  moves=" << mn << "-" << mx
             << "  avg_nodes=" << (long long)(tnodes/ok);
    cout << "\n==========================================\n\n" << flush;
}

// ================================================================
//  Main
// ================================================================
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    srand((unsigned)time(nullptr));

    cout << "\n";
    cout << "  +-----------------------------------------------------+\n";
    cout << "  |      RUBIK'S CUBE SOLVER  --  IDA* + Pattern DBs    |\n";
    cout << "  |  18 moves | Corner DB (85MB) | Edge-6 DBs (2x40MB)  |\n";
    cout << "  +-----------------------------------------------------+\n";
    cout << "\n" << flush;

    PatternDB db;
    if (!db.load()) db.build();

    Cube cube;

    while (true) {
        cout << "+--------------------------------------+\n";
        cout << "|  1. Random scramble                  |\n";
        cout << "|  2. Custom scramble                  |\n";
        cout << "|  3. Solve current cube               |\n";
        cout << "|  4. Show current cube                |\n";
        cout << "|  5. Reset to solved                  |\n";
        cout << "|  6. Benchmark                        |\n";
        cout << "|  7. Exit                             |\n";
        cout << "+--------------------------------------+\n";
        cout << "  Choice: " << flush;

        int ch;
        if (!(cin >> ch)) break;
        cin.ignore();

        if (ch == 1) {
            int n;
            cout << "  Scramble moves (e.g. 15): " << flush;
            cin >> n; cin.ignore();
            cube.reset();
            string sc = randScramble(n);
            vector<int> smv = parseSeq(sc);
            for (int i=0;i<(int)smv.size();i++) cube.apply(smv[i]);
            cout << "\n  Scramble: " << sc << "\n" << flush;
            cube.print();

        } else if (ch == 2) {
            cout << "  Enter moves (e.g.  U R2 F' D B2 L'): " << flush;
            string s; getline(cin, s);
            cube.reset();
            vector<int> smv = parseSeq(s);
            for (int i=0;i<(int)smv.size();i++) cube.apply(smv[i]);
            cout << "\n  Applied: " << s << "\n" << flush;
            cube.print();

        } else if (ch == 3) {
            if (cube.solved()) { cout << "\n  Already solved!\n\n" << flush; continue; }
            cout << "\n" << flush;
            auto t0 = chrono::steady_clock::now();
            vector<int> sol = solveWith(cube, db, 25);
            double ms = chrono::duration<double,milli>(
                chrono::steady_clock::now()-t0).count();
            if (!sol.empty()) {
                cout << "\n  Solution (" << sol.size() << " moves):\n  ";
                printSol(sol);
                cout << "  Time: " << ms << "ms  Nodes: " << gNodes << "\n";
                Cube v = cube;
                for (int i=0;i<(int)sol.size();i++) v.apply(sol[i]);
                cout << "  Check: " << (v.solved() ? "CORRECT" : "ERROR") << "\n\n" << flush;
            } else {
                cout << "  No solution found.\n\n" << flush;
            }

        } else if (ch == 4) {
            cube.print();
            cout << "  Status: " << (cube.solved() ? "SOLVED" : "Scrambled")
                 << "\n\n" << flush;

        } else if (ch == 5) {
            cube.reset();
            cout << "  Reset to solved.\n\n" << flush;

        } else if (ch == 6) {
            int t, d;
            cout << "  Number of tests: " << flush; cin >> t;
            cout << "  Scramble depth:  " << flush; cin >> d; cin.ignore();
            runBench(db, t, d);

        } else if (ch == 7) {
            cout << "  Goodbye!\n\n" << flush; break;
        } else {
            cout << "  Invalid. Enter 1-7.\n\n" << flush;
        }
    }
    return 0;
}
