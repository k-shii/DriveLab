#include <ncurses.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <locale.h>
#include <signal.h>
#include <sys/stat.h>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "demo_ui.h"

using Clock = std::chrono::steady_clock;

static bool g_demo_mode = false;

struct Drive {
    std::string name, path, model, serial, type, tran;
    unsigned long long size = 0;
    std::string state = "READY";
    std::string protection_reason;
    bool mounted = false;
};

struct SmartSummary {
    std::string health = "UNKNOWN";
    long long realloc = -1;
    long long pending = -1;
    long long offline_unc = -1;
    long long crc = -1;
    long long poh = -1;
    long long temp = -1;
};

struct DiskStats {
    unsigned long long read_ios = 0;
    unsigned long long read_sectors = 0;
    unsigned long long write_ios = 0;
    unsigned long long write_sectors = 0;
    unsigned long long in_flight = 0;
    unsigned long long io_ms = 0;
    bool valid = false;
};

struct BenchResult {
    std::string name;
    double avg_mib_s = 0.0;
    double avg_iops = 0.0;
    std::string fio_line;
    std::string clat_line;
    std::string p99_line;
    bool ok = false;
};

struct Profile {
    std::string name;
    std::string rw;
    std::string bs;
    int qd = 1;
    int runtime = 20;
    unsigned long long offset = 0;
    unsigned long long size = 0;
};

static std::string trim(std::string s) {
    auto notsp = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
    s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
    return s;
}

static std::string shell(const std::string& cmd) {
    std::string out;
    if (g_demo_mode) return out;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

static bool commandExists(const std::string& cmd) {
    if (g_demo_mode) return false;
    return system(("command -v " + cmd + " >/dev/null 2>&1").c_str()) == 0;
}

static std::map<std::string,std::string> parsePairs(const std::string& line) {
    std::map<std::string,std::string> m;
    static const std::regex re("([A-Z0-9_]+)=\"([^\"]*)\"");
    for (auto it = std::sregex_iterator(line.begin(), line.end(), re);
         it != std::sregex_iterator(); ++it) {
        m[(*it)[1].str()] = (*it)[2].str();
    }
    return m;
}

static std::vector<Drive> getDrives() {
    if (g_demo_mode) {
        return {
            {"demo0", "/dev/demo0", "WDC WD80EFZZ (demo)", "DL-READY-001", "disk", "sata", 8001563222016ULL, "READY", "", false},
            {"demo1", "/dev/demo1", "Samsung SSD 870 EVO (demo)", "DL-SYSTEM-002", "disk", "sata", 1000204886016ULL, "PROTECTED", "System disk: contains /, /boot, and active swap", true},
            {"demo2", "/dev/demo2", "Seagate Exos X18 (demo)", "DL-VM-003", "disk", "sas", 18000207937536ULL, "PROTECTED", "Assigned as a raw disk to Proxmox VM 104", false},
            {"demo3", "/dev/demo3", "TOSHIBA MG08ACA16TE (demo)", "DL-FAIL-004", "disk", "sata", 16000900661248ULL, "FAILING", "", false}
        };
    }
    std::vector<Drive> v;
    std::string out = shell("lsblk -dnP -b -o NAME,PATH,SIZE,MODEL,SERIAL,TYPE,TRAN 2>/dev/null");
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        auto m = parsePairs(line);
        if (m["TYPE"] != "disk") continue;
        Drive d;
        d.name = m["NAME"]; d.path = m["PATH"]; d.model = trim(m["MODEL"]);
        d.serial = trim(m["SERIAL"]); d.type = m["TYPE"]; d.tran = m["TRAN"];
        try { d.size = std::stoull(m["SIZE"]); } catch (...) { d.size = 0; }
        v.push_back(d);
    }
    return v;
}

static long long attrRaw(const std::string& text, const std::string& attr) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(attr) == std::string::npos) continue;
        std::istringstream ls(line);
        std::vector<std::string> toks;
        std::string t;
        while (ls >> t) toks.push_back(t);
        if (!toks.empty()) {
            try { return std::stoll(toks.back()); } catch (...) {}
        }
    }
    return -1;
}

static SmartSummary readSmart(const Drive& d) {
    SmartSummary s;
    if (g_demo_mode) {
        s.health = d.state == "FAILING" ? "FAILED" : "PASSED";
        s.realloc = d.state == "FAILING" ? 284 : 0;
        s.pending = d.state == "FAILING" ? 37 : 0;
        s.offline_unc = d.state == "FAILING" ? 12 : 0;
        s.crc = d.state == "FAILING" ? 9 : 0;
        s.poh = d.state == "FAILING" ? 51842 : 8421;
        s.temp = d.state == "FAILING" ? 51 : 34;
        return s;
    }
    std::string out = shell("smartctl -H -A " + d.path + " 2>/dev/null");
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("SMART overall-health self-assessment test result:") != std::string::npos ||
            line.find("SMART Health Status:") != std::string::npos) {
            auto p = line.find(':');
            if (p != std::string::npos) s.health = trim(line.substr(p+1));
        }
    }
    s.realloc = attrRaw(out, "Reallocated_Sector_Ct");
    s.pending = attrRaw(out, "Current_Pending_Sector");
    s.offline_unc = attrRaw(out, "Offline_Uncorrectable");
    s.crc = attrRaw(out, "UDMA_CRC_Error_Count");
    s.poh = attrRaw(out, "Power_On_Hours");
    s.temp = attrRaw(out, "Temperature_Celsius");
    if (s.temp < 0) s.temp = attrRaw(out, "Airflow_Temperature_Cel");
    return s;
}

static bool hasMountedChildren(const Drive& d) {
    if (g_demo_mode) return d.mounted;
    std::string out = shell("lsblk -nr -o MOUNTPOINT " + d.path + " 2>/dev/null");
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss,line)) if (!trim(line).empty()) return true;
    return false;
}

static std::string humanBytes(unsigned long long b) {
    const char* u[] = {"B","KiB","MiB","GiB","TiB","PiB"};
    double x = static_cast<double>(b); int i=0;
    while (x >= 1024.0 && i < 5) { x /= 1024.0; ++i; }
    std::ostringstream o; o << std::fixed << std::setprecision(i ? 1 : 0) << x << ' ' << u[i];
    return o.str();
}

static DiskStats getStats(const std::string& name) {
    DiskStats s;
    if (g_demo_mode) return s;
    std::ifstream f("/sys/class/block/" + name + "/stat");
    if (!f) return s;
    std::vector<unsigned long long> a;
    unsigned long long x;
    while (f >> x) a.push_back(x);
    if (a.size() >= 10) {
        s.read_ios = a[0]; s.read_sectors = a[2];
        s.write_ios = a[4]; s.write_sectors = a[6];
        s.in_flight = a[8]; s.io_ms = a[9]; s.valid = true;
    }
    return s;
}

static void boxTitle(WINDOW* w, const std::string& title) {
    box(w, 0, 0);
    wattron(w, COLOR_PAIR(2) | A_BOLD);
    mvwprintw(w, 0, 2, " %s ", title.c_str());
    wattroff(w, COLOR_PAIR(2) | A_BOLD);
}

static std::string fit(const std::string& s, int n) {
    if (n <= 0) return "";
    if ((int)s.size() <= n) return s;
    if (n <= 3) return s.substr(0,n);
    return s.substr(0,n-3) + "...";
}

static std::string val(long long x) { return x < 0 ? "n/a" : std::to_string(x); }

static bool isProtected(const Drive& d) { return d.state == "PROTECTED"; }

static void drawMain(const std::vector<Drive>& drives, int sel, const SmartSummary& smart, const std::string& msg) {
    erase();
    int h,w; getmaxyx(stdscr,h,w);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 2, "DriveLab // Storage Bench%s", g_demo_mode ? " [DEMO]" : "");
    attroff(COLOR_PAIR(1) | A_BOLD);
    bool protectedSelected = !drives.empty() && sel >= 0 && sel < (int)drives.size() && isProtected(drives[sel]);
    if (g_demo_mode)
        mvprintw(1, 2, "DEMO: no device commands   arrows: select   %sR: refresh   Q: quit", protectedSelected ? "" : "B: simulate   ");
    else
        mvprintw(1, 2, "READ-ONLY benchmark mode   arrows: select   %sR: refresh   Q: quit", protectedSelected ? "" : "B: bench   ");

    if (h < 18 || w < 72) {
        mvprintw(3,2,"Terminal too small. Resize to at least 72x18.");
        wnoutrefresh(stdscr); doupdate(); return;
    }

    attron(COLOR_PAIR(5));
    mvprintw(h-2,2,"%s",fit(msg,w-4).c_str());
    attroff(COLOR_PAIR(5));

    int top=3, bottom=3;
    int ph=h-top-bottom;
    bool wide = w >= 105;
    int lw = wide ? std::min(46, w/3) : w-2;
    int rw = wide ? w-lw-3 : w-2;
    int lh = wide ? ph : std::max(7, ph/2);
    int rh = wide ? ph : ph-lh-1;
    int lx=1, ly=top;
    int rx=wide ? lw+2 : 1;
    int ry=wide ? top : top+lh+1;

    WINDOW* left = newwin(lh,lw,ly,lx);
    WINDOW* right = newwin(rh,rw,ry,rx);
    boxTitle(left,"Drives");
    int row=2;
    for (int i=0;i<(int)drives.size() && row<lh-1;i++,row++) {
        const auto& d=drives[i];
        if (i==sel) wattron(left,COLOR_PAIR(3)|A_BOLD);
        std::ostringstream line;
        line << (i==sel ? "> " : "  ") << d.path << " [" << d.state << "]  " << humanBytes(d.size) << "  " << d.model;
        mvwprintw(left,row,1,"%-*s",lw-2,fit(line.str(),lw-2).c_str());
        if (i==sel) wattroff(left,COLOR_PAIR(3)|A_BOLD);
    }

    boxTitle(right,"Selected drive");
    if (!drives.empty() && sel >=0 && sel < (int)drives.size()) {
        const auto& d=drives[sel];
        int y=2;
        auto line=[&](const char* k,const std::string& v){ if(y<rh-1) mvwprintw(right,y++,2,"%-13s %s",k,fit(v,rw-18).c_str()); };
        line("Device",d.path); line("Model",d.model.empty()?"n/a":d.model); line("Serial",d.serial.empty()?"n/a":d.serial);
        line("Capacity",humanBytes(d.size)); line("Transport",d.tran.empty()?"n/a":d.tran);
        line("Status",d.state);
        if (isProtected(d)) {
            if (y<rh-1) { wattron(right,COLOR_PAIR(4)|A_BOLD); mvwprintw(right,y++,2,"Protected device"); wattroff(right,COLOR_PAIR(4)|A_BOLD); }
            line("Reason",d.protection_reason);
        }
        if (y<rh-1) y++;
        line("SMART",smart.health);
        line("Reallocated",val(smart.realloc)); line("Pending",val(smart.pending));
        line("Offline UNC",val(smart.offline_unc)); line("CRC errors",val(smart.crc));
        line("Power hours",val(smart.poh)); line("Temperature",smart.temp<0?"n/a":std::to_string(smart.temp)+" C");
        if (hasMountedChildren(d) && y<rh-1) {
            wattron(right,COLOR_PAIR(4)|A_BOLD); mvwprintw(right,y++,2,"MOUNTED: yes (writes must stay disabled)"); wattroff(right,COLOR_PAIR(4)|A_BOLD);
        }
    }
    wnoutrefresh(stdscr);
    wnoutrefresh(left);
    wnoutrefresh(right);
    doupdate();
    delwin(left); delwin(right);
}

static std::string sparkline(const std::deque<double>& hist, int width) {
    static const char levels[] = " .:-=+*#%@";
    if (width <= 0 || hist.empty()) return "";
    double mx = *std::max_element(hist.begin(),hist.end());
    if (mx <= 0) mx=1;
    int start = std::max(0,(int)hist.size()-width);
    std::string s;
    for (int i=start;i<(int)hist.size();++i) {
        int idx = std::clamp((int)std::round(hist[i]/mx*9.0),0,9);
        s.push_back(levels[idx]);
    }
    if ((int)s.size()<width) s=std::string(width-s.size(),' ')+s;
    return s;
}

static void drawBench(const Drive& d, const Profile& p, int elapsed, double rmb, double wmb,
                      double riops, double wiops, double util, unsigned long long inflight,
                      const std::deque<double>& hist, int testNo, int testTotal) {
    erase(); int h,w; getmaxyx(stdscr,h,w);
    attron(COLOR_PAIR(1)|A_BOLD); mvprintw(0,2,"DriveLab // %s", g_demo_mode ? "SIMULATED BENCH [DEMO]" : "LIVE BENCH"); attroff(COLOR_PAIR(1)|A_BOLD);
    mvprintw(1,2,"%s  |  %s  |  serial %s",d.path.c_str(),fit(d.model,32).c_str(),d.serial.c_str());
    mvprintw(2,2,"Test %d/%d: %s   [%s, QD%d]",testNo,testTotal,p.name.c_str(),p.bs.c_str(),p.qd);

    int barw=std::max(10,w-24);
    double frac=std::min(1.0, elapsed/(double)std::max(1,p.runtime));
    int filled=(int)(frac*barw);
    mvprintw(4,2,"Progress [");
    attron(COLOR_PAIR(3)); for(int i=0;i<filled;i++) addch('#'); attroff(COLOR_PAIR(3));
    for(int i=filled;i<barw;i++) addch('-');
    printw("] %3d%%",(int)(frac*100));

    mvprintw(6,2,"Read     %9.2f MiB/s    %10.1f IOPS",rmb,riops);
    mvprintw(7,2,"Write    %9.2f MiB/s    %10.1f IOPS",wmb,wiops);
    mvprintw(8,2,"Util     %9.1f %%        in-flight: %llu",util,inflight);
    mvprintw(10,2,"Throughput history (autoscale)");
    std::string sp=sparkline(hist,std::max(10,w-4));
    attron(COLOR_PAIR(2)|A_BOLD); mvprintw(11,2,"%s",sp.c_str()); attroff(COLOR_PAIR(2)|A_BOLD);

    if (h>16) {
        mvprintw(14,2,"%s", g_demo_mode ? "Simulation only. No storage command or device I/O is running." : "Raw device READ ONLY. fio is running with --readonly.");
        mvprintw(15,2,"Press X to abort the current %s.", g_demo_mode ? "simulation" : "fio process");
    }
    refresh();
}

static std::string interestingLine(const std::string& out, const std::string& needle) {
    std::istringstream iss(out); std::string line;
    while (std::getline(iss,line)) if (line.find(needle)!=std::string::npos) return trim(line);
    return "";
}

static BenchResult runProfile(const Drive& d, const Profile& p, int testNo, int testTotal) {
    BenchResult br; br.name=p.name;
    if (g_demo_mode) {
        std::deque<double> hist;
        double sumMib = 0.0, sumIops = 0.0;
        int samples = 0;
        bool aborted = false;
        nodelay(stdscr,TRUE);
        for (int tick=0; tick<=40; ++tick) {
            double phase = tick / 40.0;
            double wave = 0.92 + 0.08 * std::sin(tick * 0.7);
            bool random = p.rw.find("rand") != std::string::npos;
            double rmb = (random ? (p.qd == 1 ? 0.72 : 6.4) : 188.0) * wave;
            if (d.state == "FAILING") rmb *= 0.38 + 0.12 * std::sin(tick * 1.9);
            double riops = random ? rmb * 256.0 : rmb;
            double util = std::min(100.0, random ? 82.0 + 8.0 * wave : 94.0 + 4.0 * wave);
            hist.push_back(rmb); if(hist.size()>240) hist.pop_front();
            sumMib += rmb; sumIops += riops; ++samples;
            drawBench(d,p,(int)std::round(phase*p.runtime),rmb,0.0,riops,0.0,util,p.qd,hist,testNo,testTotal);
            int c=getch();
            if(c=='x'||c=='X'){ aborted=true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        nodelay(stdscr,FALSE);
        br.avg_mib_s=sumMib/std::max(1,samples); br.avg_iops=sumIops/std::max(1,samples);
        br.fio_line=aborted ? "simulation aborted" : "simulated workload complete; no fio executed";
        br.clat_line="simulated latency data";
        br.p99_line="simulated p99 data";
        br.ok=!aborted;
        return br;
    }
    std::string tmp="/tmp/drivelab-fio-"+std::to_string(getpid())+"-"+std::to_string(testNo)+".txt";
    std::vector<std::string> args = {
        "fio", "--name="+p.name, "--filename="+d.path, "--rw="+p.rw, "--bs="+p.bs,
        "--ioengine=libaio", "--direct=1", "--iodepth="+std::to_string(p.qd), "--numjobs=1",
        "--runtime="+std::to_string(p.runtime), "--time_based=1", "--group_reporting=1", "--readonly",
        "--output="+tmp
    };
    if (p.offset) args.push_back("--offset="+std::to_string(p.offset));
    if (p.size) args.push_back("--size="+std::to_string(p.size));

    pid_t pid=fork();
    if(pid==0){
        std::vector<char*> av; for(auto& s:args) av.push_back(s.data()); av.push_back(nullptr);
        execvp("fio",av.data()); _exit(127);
    }
    if(pid<0) return br;

    DiskStats prev=getStats(d.name);
    auto tprev=Clock::now(), t0=tprev;
    std::deque<double> hist;
    int status=0; bool done=false; double sumT=0, sumR=0, sumW=0, sumRI=0, sumWI=0;
    nodelay(stdscr,TRUE);
    while(!done){
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto now=Clock::now(); double dt=std::chrono::duration<double>(now-tprev).count();
        DiskStats cur=getStats(d.name);
        double rmb=0,wmb=0,ri=0,wi=0,util=0;
        if(cur.valid && prev.valid && dt>0){
            rmb=(cur.read_sectors-prev.read_sectors)*512.0/dt/1048576.0;
            wmb=(cur.write_sectors-prev.write_sectors)*512.0/dt/1048576.0;
            ri=(cur.read_ios-prev.read_ios)/dt; wi=(cur.write_ios-prev.write_ios)/dt;
            util=std::min(100.0,(cur.io_ms-prev.io_ms)/(dt*10.0));
            sumT+=dt; sumR+=rmb*dt; sumW+=wmb*dt; sumRI+=ri*dt; sumWI+=wi*dt;
        }
        hist.push_back(rmb+wmb); if(hist.size()>240) hist.pop_front();
        int elapsed=(int)std::chrono::duration_cast<std::chrono::seconds>(now-t0).count();
        drawBench(d,p,elapsed,rmb,wmb,ri,wi,util,cur.in_flight,hist,testNo,testTotal);
        int c=getch(); if(c=='x'||c=='X'){ kill(pid,SIGINT); }
        pid_t wr=waitpid(pid,&status,WNOHANG); if(wr==pid) done=true;
        prev=cur; tprev=now;
    }
    nodelay(stdscr,FALSE);
    if(sumT>0){ br.avg_mib_s=(sumR+sumW)/sumT; br.avg_iops=(sumRI+sumWI)/sumT; }
    std::string out=shell("cat "+tmp+" 2>/dev/null"); unlink(tmp.c_str());
    br.fio_line=interestingLine(out, p.rw.find("read")!=std::string::npos ? "read: IOPS=" : "write: IOPS=");
    br.clat_line=interestingLine(out,"clat (");
    br.p99_line=interestingLine(out,"99.00th=");
    br.ok=WIFEXITED(status)&&WEXITSTATUS(status)==0;
    return br;
}

static unsigned long long alignMiB(unsigned long long x){ return (x/(1024ULL*1024ULL))*(1024ULL*1024ULL); }

static std::vector<Profile> makeSuite(const Drive& d) {
    const unsigned long long MiB=1024ULL*1024ULL, GiB=1024ULL*MiB;
    unsigned long long guard=1*MiB;
    unsigned long long chunk=std::min<unsigned long long>(8*GiB,std::max<unsigned long long>(512*MiB,d.size/20));
    if(d.size < chunk + 2*guard) chunk = d.size > 4*guard ? d.size-2*guard : 0;
    chunk=alignMiB(chunk);
    unsigned long long outer=guard;
    unsigned long long mid=alignMiB(d.size/2 - chunk/2);
    unsigned long long inner=d.size>chunk+guard ? alignMiB(d.size-chunk-guard) : guard;
    unsigned long long randomSize=d.size>2*guard ? alignMiB(d.size-2*guard) : 0;
    return {
        {"seq_outer","read","1M",16,20,outer,chunk},
        {"seq_middle","read","1M",16,20,mid,chunk},
        {"seq_inner","read","1M",16,20,inner,chunk},
        {"rand4k_qd1","randread","4k",1,25,guard,randomSize},
        {"rand4k_qd32","randread","4k",32,25,guard,randomSize}
    };
}

static std::string safeName(std::string s) {
    if (s.empty()) return "unknown";
    for (char& c : s) if (!std::isalnum((unsigned char)c) && c!='-' && c!='_') c='_';
    return s;
}

static std::string saveReport(const Drive& d, const SmartSummary& before,
                              const SmartSummary& after, const std::vector<BenchResult>& rs) {
    if (g_demo_mode) return "";
    mkdir("/root/drivelab-reports", 0755);
    std::time_t tt=std::time(nullptr); std::tm tm{}; localtime_r(&tt,&tm);
    char ts[32]; std::strftime(ts,sizeof(ts),"%Y%m%d-%H%M%S",&tm);
    std::string id=safeName(d.serial.empty()?d.name:d.serial);
    std::string path="/root/drivelab-reports/"+id+"-"+ts+".txt";
    std::ofstream f(path);
    if(!f) return "";
    f << "DriveLab read-only benchmark report\n";
    f << "Device: " << d.path << "\nModel: " << d.model << "\nSerial: " << d.serial
      << "\nCapacity: " << d.size << " bytes\nTransport: " << d.tran << "\n\n";
    auto sm=[&](const char* label,const SmartSummary& s){
        f << label << " SMART: health=" << s.health << " realloc=" << s.realloc
          << " pending=" << s.pending << " offline_unc=" << s.offline_unc
          << " crc=" << s.crc << " power_hours=" << s.poh << " temp=" << s.temp << "C\n";
    };
    sm("Before",before); sm("After",after); f << "\n";
    for(const auto& r:rs){
        f << "[" << r.name << "] ok=" << (r.ok?"yes":"no")
          << " avg_mib_s=" << std::fixed << std::setprecision(2) << r.avg_mib_s
          << " avg_iops=" << std::setprecision(1) << r.avg_iops << "\n";
        if(!r.fio_line.empty()) f << "  " << r.fio_line << "\n";
        if(!r.clat_line.empty()) f << "  " << r.clat_line << "\n";
        if(!r.p99_line.empty()) f << "  p99 " << r.p99_line << "\n";
    }
    return path;
}

static void showResults(const Drive& d, const std::vector<BenchResult>& rs, const std::string& report) {
    erase(); int h,w; getmaxyx(stdscr,h,w);
    attron(COLOR_PAIR(1)|A_BOLD); mvprintw(0,2,"DriveLab // %sRESULTS", g_demo_mode ? "DEMO " : ""); attroff(COLOR_PAIR(1)|A_BOLD);
    mvprintw(1,2,"%s  %s  [%s]",d.path.c_str(),fit(d.model,40).c_str(),d.serial.c_str());
    mvprintw(3,2,"%-18s %12s %12s  %s","Test","Avg MiB/s","Avg IOPS",g_demo_mode ? "simulation summary" : "fio summary");
    mvhline(4,2,ACS_HLINE,std::max(1,w-4));
    int y=5;
    for(const auto& r:rs){
        if(y>=h-4) break;
        mvprintw(y++,2,"%-18s %12.2f %12.1f  %s",r.name.c_str(),r.avg_mib_s,r.avg_iops,fit(r.fio_line,w-50).c_str());
        if(!r.clat_line.empty() && y<h-4) mvprintw(y++,6,"%s",fit(r.clat_line,w-8).c_str());
        if(!r.p99_line.empty() && y<h-4) mvprintw(y++,6,"p99: %s",fit(r.p99_line,w-13).c_str());
    }
    if(!report.empty() && h>3) mvprintw(h-3,2,"Report: %s",fit(report,w-10).c_str());
    mvprintw(h-2,2,"Press any key to return."); refresh(); getch();
}

static bool confirmBench(const Drive& d) {
    erase(); int h,w; getmaxyx(stdscr,h,w);
    attron(COLOR_PAIR(2)|A_BOLD); mvprintw(1,2,"%s", g_demo_mode ? "SIMULATED BENCHMARK [DEMO]" : "READ-ONLY RAW DISK BENCHMARK"); attroff(COLOR_PAIR(2)|A_BOLD);
    mvprintw(3,2,"Device: %s",d.path.c_str()); mvprintw(4,2,"Model : %s",d.model.c_str()); mvprintw(5,2,"Serial: %s",d.serial.c_str());
    mvprintw(7,2,"Suite: outer/middle/inner sequential + 4K random QD1/QD32.");
    mvprintw(8,2,"%s", g_demo_mode ? "No storage command or device I/O will be executed." : "fio will be launched with --readonly; no write workload is included.");
    if(hasMountedChildren(d)) { attron(COLOR_PAIR(4)|A_BOLD); mvprintw(10,2,"Drive has mounted filesystem(s). Read tests are still non-destructive, but expect workload impact."); attroff(COLOR_PAIR(4)|A_BOLD); }
    mvprintw(12,2,"Press ENTER to run, or ESC to cancel."); refresh();
    int c; while((c=getch())!=27 && c!='\n' && c!=KEY_ENTER){} return c!=27;
}

int main(int argc, char** argv){
    for (int i=1; i<argc; ++i) {
        std::string arg=argv[i];
        if (arg=="--demo") g_demo_mode=true;
        else { std::cerr<<"Usage: "<<argv[0]<<" [--demo]\n"; return 1; }
    }
    if(!g_demo_mode && geteuid()!=0){ std::cerr<<"DriveLab must run as root for raw block-device fio reads.\n"; return 1; }
    if(!g_demo_mode && (!commandExists("fio") || !commandExists("smartctl") || !commandExists("lsblk"))) {
        std::cerr<<"Missing dependency. Install: apt install fio smartmontools util-linux\n"; return 1;
    }
    setlocale(LC_ALL,"");
    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE); curs_set(0);
    if(has_colors()){ start_color(); use_default_colors();
        init_pair(1,COLOR_CYAN,-1); init_pair(2,COLOR_BLUE,-1); init_pair(3,COLOR_GREEN,-1);
        init_pair(4,COLOR_RED,-1); init_pair(5,COLOR_YELLOW,-1);
    }
    if(g_demo_mode){
        int result=runDemoUi();
        endwin();
        return result;
    }
    std::vector<Drive> drives=getDrives(); int sel=0; SmartSummary smart;
    std::string msg=g_demo_mode ? "Demo inventory loaded; all jobs are simulated." : "Ready.";
    if(!drives.empty()) smart=readSmart(drives[0]);
    bool quit=false;
    while(!quit){
        if(sel >= (int)drives.size()) sel=std::max(0,(int)drives.size()-1);
        drawMain(drives,sel,smart,msg);
        int c=getch();
        if(c=='q'||c=='Q') quit=true;
        else if(c==KEY_UP && sel>0){ --sel; smart=readSmart(drives[sel]); msg="Selected "+drives[sel].path; }
        else if(c==KEY_DOWN && sel+1<(int)drives.size()){ ++sel; smart=readSmart(drives[sel]); msg="Selected "+drives[sel].path; }
        else if(c=='r'||c=='R'){
            std::string old=drives.empty()?"":drives[sel].serial; drives=getDrives(); sel=0;
            for(int i=0;i<(int)drives.size();++i) if(!old.empty()&&drives[i].serial==old){sel=i;break;}
            if(!drives.empty()) smart=readSmart(drives[sel]);
            msg="Drive list + SMART refreshed.";
        }
        else if((c=='b'||c=='B') && !drives.empty()){
            Drive d=drives[sel];
            if(isProtected(d)) {
                msg="Protected device: "+d.protection_reason;
            } else if(confirmBench(d)){
                SmartSummary before=readSmart(d);
                auto suite=makeSuite(d); std::vector<BenchResult> rs;
                for(int i=0;i<(int)suite.size();++i) rs.push_back(runProfile(d,suite[i],i+1,suite.size()));
                SmartSummary after=readSmart(d);
                std::string report=saveReport(d,before,after,rs);
                showResults(d,rs,report); smart=after;
                if(g_demo_mode) msg="Simulated benchmark complete; no commands executed.";
                else msg=report.empty()?"Benchmark complete; report write failed.":"Benchmark complete. Report: "+report;
            } else msg="Benchmark cancelled.";
        }
        else if(c==KEY_RESIZE) msg="Terminal resized.";
    }
    endwin(); return 0;
}
