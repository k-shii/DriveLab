#include "test_support.h"
#include "platform/linux/linux_host_usage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

using namespace drivelab;

namespace {
struct State {
    std::string content;
    std::size_t offset = 0;
    std::string path;
    int descriptors = 0;
    int open_error = 0;
    int read_error = 0;
    int stat_error = 0;
    bool interrupted = false;
    bool block = true;
} fake;

void reset() { DL_CHECK(fake.descriptors == 0); fake = {}; }

void testReadsAndFailures() {
    ProcHostFiles files;
    reset(); fake.content = "some kernel metadata\n";
    auto text = files.read(HostTextFile::MountInfo);
    DL_CHECK(text.value == fake.content && text.issues.empty());
    DL_CHECK(fake.path == "/proc/self/mountinfo");
    DL_CHECK(fake.descriptors == 0);
    reset(); fake.content = "swap header\n"; fake.interrupted = true;
    DL_CHECK(files.read(HostTextFile::Swaps).value == fake.content);
    DL_CHECK(fake.path == "/proc/swaps");
    reset(); fake.open_error = EACCES;
    text = files.read(HostTextFile::MountInfo);
    DL_CHECK(!text.value && text.issues.front().native_error == EACCES);
    reset(); fake.content = std::string(5000, 'x'); fake.read_error = EIO;
    text = files.read(HostTextFile::MountInfo);
    DL_CHECK(!text.value && text.issues.front().native_error == EIO);
    DL_CHECK(fake.descriptors == 0);
    reset(); fake.content = std::string(16 * 1024 * 1024 + 1, 'x');
    DL_CHECK(!files.read(HostTextFile::MountInfo).value);
    DL_CHECK(fake.descriptors == 0);
}

void testStatOnly() {
    ProcHostFiles files;
    reset();
    auto value = files.inspect("/dev/by-alias");
    DL_CHECK(value.value && value.value->is_block);
    DL_CHECK((value.value->device_number == BlockDeviceNumber{8, 17}));
    DL_CHECK(fake.descriptors == 0);
    fake.block = false;
    value = files.inspect("/swapfile");
    DL_CHECK(value.value && !value.value->is_block && !value.value->device_number);
    fake.stat_error = ENOENT;
    value = files.inspect("/dev/gone");
    DL_CHECK(!value.value && value.issues.front().code == DiscoveryIssueCode::Disappeared);
    fake.stat_error = EACCES;
    DL_CHECK(files.inspect("/dev/denied").issues.front().native_error == EACCES);
    DL_CHECK(!files.inspect("relative").value);
    DL_CHECK(!files.inspect(std::string("/dev/evil\0tail", 14)).value);
}
}  // namespace

extern "C" {
int open(const char* path, int flags, ...) {
    // Any attempt to open a block node would fail this test immediately.
    DL_CHECK(std::string(path) == "/proc/self/mountinfo" || std::string(path) == "/proc/swaps");
    DL_CHECK(flags == (O_RDONLY | O_CLOEXEC));
    if (fake.open_error) { errno = fake.open_error; return -1; }
    fake.path = path; ++fake.descriptors; return 97;
}
std::ptrdiff_t read(int descriptor, void* buffer, std::size_t count) {
    DL_CHECK(descriptor == 97);
    if (fake.interrupted) { fake.interrupted = false; errno = EINTR; return -1; }
    if (fake.read_error && fake.offset > 0) { errno = fake.read_error; return -1; }
    const auto size = std::min(count, fake.content.size() - fake.offset);
    std::memcpy(buffer, fake.content.data() + fake.offset, size);
    fake.offset += size;
    return static_cast<std::ptrdiff_t>(size);
}
int close(int descriptor) { DL_CHECK(descriptor == 97); --fake.descriptors; return 0; }
int stat(const char*, struct stat* value) {
    if (fake.stat_error) { errno = fake.stat_error; return -1; }
    value->st_mode = fake.block ? fake_block_mode : 0100000;
    value->st_rdev = (std::uint64_t{8} << 32) | 17;
    return 0;
}
}

int main() {
    return test::run([] { testReadsAndFailures(); testStatOnly(); reset(); });
}
