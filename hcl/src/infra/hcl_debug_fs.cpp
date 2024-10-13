#ifdef DEB_FS

#include "hcl_debug_fs.h"
#include "../hcl_utils.h"
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <linux/mman.h>

static std::string readFile(const std::string& filePath)
{
    std::string   result;
    std::ifstream file(filePath);

    if (file.is_open())
    {
        std::ostringstream sstr;
        sstr << file.rdbuf();
        file.close();

        result = sstr.str();
    }

    return result;
}

hcl_debug_fs::hcl_debug_fs()
{
    const std::string parent_dev = readFile("/sys/class/accel/accel0/device/parent_device");

    const std::string addr = "//sys/kernel/debug/accel/" + parent_dev + "/addr";
    const std::string data = "//sys/kernel/debug/accel/" + parent_dev + "/data32";

    m_addr_fd = open(addr.c_str(), O_WRONLY);
    m_data_fd = open(data.c_str(), O_RDWR);

    VERIFY(m_addr_fd > 0);
    VERIFY(m_data_fd > 0);
}

hcl_debug_fs::~hcl_debug_fs()
{
    close(m_addr_fd);
    close(m_data_fd);
}

int hcl_debug_fs::read_cmd(uint64_t full_address, uint32_t& val)
{
    char        addr_str[64] = {0}, value[64] = {0};
    std::string val_str;

    sprintf(addr_str, "0x%lx", full_address);

    ssize_t bytes_written = write(m_addr_fd, addr_str, strlen(addr_str) + 1);

    VERIFY(bytes_written == (ssize_t)strlen(addr_str) + 1);

    ssize_t bytes_read = pread(m_data_fd, value, sizeof(value), 0);
    VERIFY(bytes_read >= 1);

    val_str = value;

    val = stol(val_str, nullptr, 16);

    return 0;
}

int hcl_debug_fs::write_cmd(uint64_t full_address, uint32_t val)
{
    char addr_str[64] = {0}, val_str[64] = {0};

    sprintf(addr_str, "0x%lx", full_address);
    sprintf(val_str, "0x%x", val);

    ssize_t bytes_written = write(m_addr_fd, addr_str, strlen(addr_str) + 1);

    VERIFY(bytes_written == (ssize_t)strlen(addr_str) + 1);

    bytes_written = write(m_data_fd, val_str, strlen(val_str) + 1);
    VERIFY(bytes_written == (ssize_t)strlen(val_str) + 1);

    return 0;
}

#endif