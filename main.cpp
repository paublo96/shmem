#include "umpire/Umpire.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>


class SHMEMResource : public umpire::resource::MemoryResource
{
public:
    SHMEMResource(umpire::Platform /*platform*/, const std::string &name,
                  int id, umpire::MemoryResourceTraits traits)
        : MemoryResource(name, id, traits) {}

    umpire::Platform getPlatform() noexcept { return umpire::Platform::host; }

    void *allocate(std::size_t bytes)
    { throw std::runtime_error("Must use allocate_named(...)"); }
    void *allocate_named(const std::string &name, std::size_t bytes)
    {
        constexpr int oflag{O_RDWR | O_CREAT | O_EXCL};
        constexpr int omode{S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH};

        bool owner = false;
        auto fd = shm_open(name.c_str(), (O_RDWR | O_CREAT | O_EXCL), omode);
        if (fd >= 0)
        {
            // Created shmem file. Resize to desired size
            ftruncate(fd, bytes);
            owner = true;
        }
        else
        {
            // Access existing shmem file. Wait for file to resize
            fd = shm_open(name.c_str(), O_RDWR, omode);
            if (fd < 0) throw std::runtime_error("Failed to create shmem");

            off_t filesize = 0;
            while (filesize == 0)
            {
                struct stat st;
                fstat(fd, &st);
                filesize = st.st_size;
                std::this_thread::yield();
            }
        }
        auto ptr = mmap(nullptr, bytes, (PROT_WRITE | PROT_READ),
                        MAP_SHARED, fd, 0);

        name_to_ptr_[name] = ptr;
        ptr_to_name_[ptr] = name;
        ptr_to_size_[ptr] = bytes;
        ptr_to_fd_[ptr] = fd;
        ptr_to_owner_[ptr] = owner;

        return ptr;
    }

    void deallocate(void *ptr, std::size_t /*size*/)
    {
        auto size = ptr_to_size_[ptr];
        auto name = ptr_to_name_[ptr];
        auto fd = ptr_to_fd_[ptr];
        auto owner = ptr_to_owner_[ptr];

        munmap(ptr, size);
        close(fd);
        if (owner) shm_unlink(name.c_str());
    }

    void *find_pointer_from_name(const std::string &name)
    {
        try
        {
            return name_to_ptr_[name];
        }
        catch (std::exception &)
        {
            constexpr int omode{S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH};
            auto fd = shm_open(name.c_str(), O_RDWR, omode);
            if (fd < 0) throw std::runtime_error("Failed to create shmem");

            off_t filesize = 0;
            while (filesize == 0)
            {
                struct stat st;
                fstat(fd, &st);
                filesize = st.st_size;
                std::this_thread::yield();
            }

            auto ptr = mmap(nullptr, std::size_t(filesize),
                            (PROT_WRITE | PROT_READ),
                            MAP_SHARED, fd, 0);

            name_to_ptr_[name] = ptr;
            ptr_to_name_[ptr] = name;
            ptr_to_size_[ptr] = filesize;
            ptr_to_fd_[ptr] = fd;
            ptr_to_owner_[ptr] = false;

            return ptr;
        }
    }

    bool isAccessibleFrom(umpire::Platform p) noexcept
    { return (p == umpire::Platform::host) ? true : false; }

private:
    std::map<std::string, void *> name_to_ptr_;
    std::map<void *, std::string> ptr_to_name_;
    std::map<void *, std::size_t> ptr_to_size_;
    std::map<void *, int>         ptr_to_fd_;
    std::map<void *, bool>        ptr_to_owner_;
};

class SHMEMResourceFactory : public umpire::resource::MemoryResourceFactory
{
    bool isValidMemoryResourceFor(const std::string &name) noexcept
    { return (name.find("SHMEM") != std::string::npos) ? true : false; }

    std::unique_ptr<umpire::resource::MemoryResource>
    create(const std::string &name, int id)
    { return create(name, id, getDefaultTraits()); }

    std::unique_ptr<umpire::resource::MemoryResource>
    create(const std::string &name, int id,
           umpire::MemoryResourceTraits traits)
    {
        return std::unique_ptr<umpire::resource::MemoryResource>(
            new SHMEMResource(umpire::Platform::host, name, id, traits));
    }

    umpire::MemoryResourceTraits
    getDefaultTraits()
    {
        using MRT = umpire::MemoryResourceTraits;
        MRT traits;

        traits.unified = false;
        traits.size = 0;

        traits.vendor   = MRT::vendor_type::unknown;
        traits.kind     = MRT::memory_type::unknown;
        traits.used_for = MRT::optimized_for::any;
        traits.resource = MRT::resource_type::shared;
        traits.scope    = MRT::shared_scope::node;

        return traits;
    }
};


int main()
{
    auto &rm = umpire::ResourceManager::getInstance();
    auto &rf = umpire::resource::MemoryResourceRegistry::getInstance();
    rf.registerMemoryResource(std::make_unique<SHMEMResourceFactory>());
    auto allocator = rm.makeResource("SHMEM::0");

    char *data = static_cast<char *>(allocator.allocate("alloc1", 12));
    data[0] = 'H';
    data[1] = 'I';
    data[2] = '\n';
    printf(data);
    sleep(10);
    allocator.deallocate(data);

    printf("DONE\n");
    return 0;
}
