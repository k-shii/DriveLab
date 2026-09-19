#include "test_support.h"
#include "ownership_fixture.h"
#include "fakes/ownership_io_api.h"
#include "platform/linux/linux_ownership_source.h"
#include <atomic>
#include <cerrno>
#include <cstring>

struct drivelab::ownership_io::Directory { std::size_t next = 0; };
namespace {
drivelab::ownership_io::Directory directory;
bool directory_open = false, list_error = false, close_error = false, fd_error = false;
int directory_closes = 0;
std::string proc_self = "77";
bool self_link_error = false, shared_thread_table = true;
std::string contents = "metadata\n";
std::size_t offset = 0;
int closes=0, opens=0;
bool denied=false, absent=false, read_error=false, interrupted=false, block=false;
unsigned special_mode=0; std::uint64_t special_number=0;
}
namespace drivelab::ownership_io {
std::ptrdiff_t readlink(const char* path, char* target, std::size_t size) {
    const auto thread_path = "/proc/" + proc_self + "/task/78/fd";
    if (std::string(path) == thread_path + "/42") {
        const auto value = shared_thread_table ? thread_path : std::string("/dev/sda");
        const auto count = std::min(size,value.size()); std::memcpy(target,value.data(),count);
        return static_cast<std::ptrdiff_t>(count);
    }
    DL_CHECK(std::string(path) == "/proc/self");
    if (self_link_error) { errno = EACCES; return -1; }
    const auto count = std::min(size, proc_self.size());
    std::memcpy(target, proc_self.data(), count);
    return static_cast<std::ptrdiff_t>(count);
}
Directory* opendir(const char* path) {
    DL_CHECK(std::string(path) == "/proc/" + proc_self + "/fd" || std::string(path) == "/proc/self/fd" || std::string(path) == "/proc/" + proc_self + "/task/78/fd");
    if (denied) { errno = EACCES; return nullptr; }
    if (absent) { errno = ENOENT; return nullptr; }
    DL_CHECK(!directory_open); directory_open = true; directory.next = 0; return &directory;
}
int dirfd(Directory* d) { DL_CHECK(d == &directory && directory_open); return 42; }
DirectoryEntry* readdir(Directory* d) {
    DL_CHECK(d == &directory && directory_open);
    static const char* names[] = {".", "..", "42", "3", "0"};
    static DirectoryEntry entry {};
    if (directory.next == 5) { errno = list_error ? EIO : 0; return nullptr; }
    const auto* name = names[directory.next++];
    std::memcpy(entry.d_name, name, std::strlen(name) + 1); return &entry;
}
int closedir(Directory* d) {
    DL_CHECK(d == &directory && directory_open); directory_open = false; ++directory_closes;
    return close_error ? -1 : 0;
}
int open(const char* path,int flags,...) {
    DL_CHECK(std::string(path)=="/sys/class/block/sda/dev");
    DL_CHECK(flags==read_flags); ++opens; offset=0;
    if (denied || absent) { errno=absent?ENOENT:EACCES; return -1; } return 42;
}
int close(int fd) { DL_CHECK(fd==42); ++closes; return 0; }
int fstat(int fd,Status* out) {
    DL_CHECK(fd==42); out->st_mode=block?0060000:0100000; return 0;
}
int stat(const char* path,Status* out) {
    if (std::string(path).starts_with("/proc/" + proc_self + "/fd/")) {
        // The observer fd would be unreadable after enumeration closes it.
        if (std::string(path).ends_with("/42") || fd_error) { errno = ENOENT; return -1; }
        out->st_mode = 0100000; out->st_dev = 1; return 0;
    }
    DL_CHECK(std::string(path)=="/dev/sda" || std::string(path)=="/var/lib/vz");
    if (denied || absent) { errno=absent?ENOENT:EACCES; return -1; }
    out->st_mode=std::string(path)=="/dev/sda"?0060000:0100000;
    out->st_rdev=static_cast<std::uint64_t>(8)<<32;
    out->st_dev=(static_cast<std::uint64_t>(253)<<32)|1;
    if (special_mode) { out->st_mode=special_mode; out->st_rdev=special_number; }
    return 0;
}
std::ptrdiff_t read(int fd,void* target,std::size_t size) {
    DL_CHECK(fd==42);
    if (interrupted) { interrupted=false; errno=EINTR; return -1; }
    if (read_error) { errno=EIO; return -1; }
    const auto length=std::min(size,contents.size()-offset);
    std::memcpy(target,contents.data()+offset,length); offset+=length;
    return static_cast<std::ptrdiff_t>(length);
}
} // namespace drivelab::ownership_io
namespace {
class ProcessIo final : public drivelab::OwnershipIo {
public:
    drivelab::NativeOwnershipIo native;
    ownership_test::Io fixture;
    drivelab::OwnershipRead<std::string> text(const std::string& p) override { return fixture.text(p); }
    drivelab::OwnershipRead<std::vector<std::string>> list(const std::string& p) override {
        return p == "/proc/" + proc_self + "/fd" ? native.list(p) : fixture.list(p);
    }
    drivelab::OwnershipRead<std::string> canonical(const std::string& p) override { return fixture.canonical(p); }
    drivelab::OwnershipRead<drivelab::OwnershipPath> locate(const std::string& p) override {
        return p.starts_with("/proc/" + proc_self + "/fd/") ? native.locate(p) : fixture.locate(p);
    }
};
void selfDescriptorCoverage() {
    ProcessIo io;
    io.fixture.lists["/proc"] = {std::vector<std::string>{"77"}, false};
    for (const auto* pid : {"77", "88", "177"})
    {
        io.fixture.lists[std::string("/proc/") + pid + "/task"] = {std::vector<std::string>{pid},false};
        io.fixture.paths[std::string("/proc/") + pid + "/cwd"] = {ownership_test::OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        io.fixture.paths[std::string("/proc/") + pid + "/root"] = {ownership_test::OwnershipPath{false,{0,1}, true, false, std::nullopt, false},false};
        io.fixture.texts[std::string("/proc/") + pid + "/maps"] = {std::string{},false};
    }
    for (const auto* path : {"/proc/77/fd/0", "/proc/77/fd/3", "/proc/177/fd/0", "/proc/177/fd/3", "/proc/88/fd/42"})
        io.fixture.links[path] = {std::string("/tmp/file"),false};
    io.fixture.links["/proc/self/ns/mnt"] = {std::string("mnt:[1]"), false};
    io.fixture.links["/proc/77/ns/mnt"] = {std::string("mnt:[1]"), false};
    io.fixture.paths["/proc"] = {drivelab::OwnershipPath{false,{0,3}, true, false, std::nullopt, false},false};
    io.fixture.texts["/proc/self/mountinfo"] = {std::string("1 0 0:1 / / rw - tmpfs tmpfs rw\n2 1 0:3 / /proc rw - proc proc rw\n"), false};
    io.fixture.texts["/proc/swaps"] = {std::string("Filename Type Size Used Priority\n"), false};
    drivelab::ResolvedInventorySnapshot inventory;
    auto scan = [&] {
        drivelab::OwnershipEvidence evidence;
        drivelab::OwnershipScanContext c{io, inventory, evidence, {}, {}, std::nullopt, false};
        drivelab::readProcessOwnership(c);
        return evidence.coverage.at("processes");
    };
    DL_CHECK(scan() && !directory_open);
    DL_CHECK(io.native.list("/proc/self/fd").value == std::vector<std::string>({"0", "3"}));
    DL_CHECK(io.native.list("/proc/77/task/78/fd").value == std::vector<std::string>({"0","3"}));
    shared_thread_table = false;
    DL_CHECK(io.native.list("/proc/77/task/78/fd").value == std::vector<std::string>({"0","3","42"}));
    shared_thread_table = true;
    self_link_error = true; DL_CHECK(!scan()); self_link_error = false;
    proc_self = "not-a-pid"; DL_CHECK(!io.native.list("/proc/77/fd").value);
    proc_self.assign(32, '7'); DL_CHECK(!io.native.list("/proc/77/fd").value);
    proc_self = "177"; // Caller PID and procfs PID need not match.
    io.fixture.lists["/proc"].value = std::vector<std::string>{"177"};
    io.fixture.links["/proc/177/ns/mnt"] = {std::string("mnt:[1]"), false};
    DL_CHECK(scan());
    proc_self = "77";
    io.fixture.lists["/proc"].value = std::vector<std::string>{"77"};
    fd_error = true; DL_CHECK(!scan()); fd_error = false;
    list_error = true; DL_CHECK(!scan() && !directory_open); list_error = false;
    close_error = true; DL_CHECK(!scan()); close_error = false;
    denied = true; DL_CHECK(!scan()); denied = false;
    absent = true; DL_CHECK(!scan()); absent = false;
    io.fixture.links["/proc/77/ns/mnt"].value = "mnt:[2]";
    DL_CHECK(!scan()); io.fixture.links["/proc/77/ns/mnt"].value = "mnt:[1]";
    // An external process's descriptor numbered 42 is NOT ours to discard.
    io.fixture.lists["/proc"].value->push_back("88");
    io.fixture.links["/proc/88/ns/mnt"] = {std::string("mnt:[1]"), false};
    io.fixture.lists["/proc/88/fd"] = {std::vector<std::string>{"42"}, false};
    DL_CHECK(!scan());
    io.fixture.paths["/proc/88/fd/42"] = {drivelab::OwnershipPath{false, {0, 1}, true, false, std::nullopt, false}, false};
    DL_CHECK(scan());
    io.fixture.lists["/proc/88/fd"] = {std::nullopt, true};
    DL_CHECK(!scan());
}
}
int main() {
    return drivelab::test::run([] {
        // Compile the standard atomic wait path that needs real Linux headers.
        // The initial value differs, so this regression cannot block.
        std::atomic<int> ready{1};
        ready.wait(0);
        ready.notify_all();
        selfDescriptorCoverage();
        drivelab::NativeOwnershipIo io;
        DL_CHECK(!io.text("/dev/sda").value && opens==0);
        interrupted=true;
        DL_CHECK(io.text("/sys/class/block/sda/dev").value==contents && closes==1);
        block=true; DL_CHECK(!io.text("/sys/class/block/sda/dev").value && closes==2); block=false;
        read_error=true; DL_CHECK(!io.text("/sys/class/block/sda/dev").value && closes==3); read_error=false;
        denied=true; auto a=io.text("/sys/class/block/sda/dev");
        DL_CHECK(!a.value && !a.absent); denied=false;
        absent=true; a=io.text("/sys/class/block/sda/dev");
        DL_CHECK(!a.value && a.absent); absent=false;
        auto b=io.locate("/dev/sda");
        DL_CHECK(b.value && b.value->block && b.value->device.major_number==8);
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && !b.value->block && b.value->device.major_number==253 && b.value->device.minor_number==1);
        // A control device lives on a filesystem but is not a regular file use.
        special_mode=0020000; special_number=static_cast<std::uint64_t>(21)<<32;
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && !b.value->filesystem_object && !b.value->known_nonstorage);
        special_number=(static_cast<std::uint64_t>(1)<<32)|3;
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && b.value->known_nonstorage); // Native /dev/null device number.
        special_number=static_cast<std::uint64_t>(136)<<32;
        b=io.locate("/var/lib/vz"); DL_CHECK(b.value && b.value->known_nonstorage); // PTY slave.
        special_number=(static_cast<std::uint64_t>(1)<<32)|1;
        b=io.locate("/var/lib/vz"); DL_CHECK(b.value && !b.value->known_nonstorage); // /dev/mem is not safe.
        // Character ownership uses the native rdev, never this deliberately
        // unrelated pathname or the containing filesystem's dev_t.
        const std::vector<drivelab::BlockDeviceNumber> reviewed{
            {1,11}, {10,130}, {10,200}, {10,232}, {10,235}, {10,236}, {10,238}, {10,242},
            {13,64}, {13,65}, {13,95}
        };
        const std::vector<drivelab::BlockDeviceNumber> opaque{
            {1,1}, {10,129}, {10,131}, {10,196}, {10,199}, {10,201}, {10,229}, {10,231},
            {10,233}, {10,234}, {10,237}, {10,239}, {10,241}, {10,243}, {13,63}, {13,96},
            {13,256}, {189,0}, {240,1}
        };
        auto character = [&](const drivelab::BlockDeviceNumber& number, bool nonstorage) {
            special_mode=0020000;
            special_number=(static_cast<std::uint64_t>(number.major_number)<<32)|number.minor_number;
            auto observation=io.locate("/var/lib/vz");
            DL_CHECK(observation.value && !observation.value->block && !observation.value->filesystem_object);
            DL_CHECK(observation.value->character_device == number);
            DL_CHECK(observation.value->known_nonstorage == nonstorage && !observation.value->socket);
            DL_CHECK((observation.value->device == drivelab::BlockDeviceNumber{253,1}));
        };
        for (const auto& number : reviewed) character(number,true);
        for (const auto& number : opaque) character(number,false);
        // Matching numbers on a block node or regular file do not grant the
        // character-device exemption.
        for (const auto mode : {0060000U,0100000U}) {
            special_mode=mode; special_number=(static_cast<std::uint64_t>(10)<<32)|242;
            b=io.locate("/var/lib/vz");
            DL_CHECK(b.value && !b.value->known_nonstorage && !b.value->character_device);
        }
        special_mode=0140000; // Native socket type cannot imply an empty SCM_RIGHTS queue.
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && b.value->socket && !b.value->known_nonstorage &&
                 !b.value->filesystem_object && !b.value->character_device);
        special_mode=0010000; // FIFO behavior remains distinct from sockets.
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && !b.value->socket && b.value->known_nonstorage && !b.value->character_device);
        special_mode=0040000;
        b=io.locate("/var/lib/vz");
        DL_CHECK(b.value && b.value->filesystem_object && !b.value->character_device && !b.value->socket);
        special_mode=0;
        contents.assign(4*1024*1024+1,'x');
        DL_CHECK(!io.text("/sys/class/block/sda/dev").value && closes==4);
    });
}
