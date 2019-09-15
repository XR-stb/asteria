// This file is part of Asteria.
// Copyleft 2018 - 2019, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "bindings_filesystem.hpp"
#include "argument_reader.hpp"
#include "simple_binding_wrapper.hpp"
#include "../runtime/global_context.hpp"
#include "../utilities.hpp"
#ifdef _WIN32
#  include <windows.h>  // ::CreateFile(), ::CloseHandle(), ::GetFileInformationByHandleEx(),
                        // ::FindFirstFile(), ::FindNextFile(), ::CreateDirectory(), ::RemoveDirectory(),
                        // ::ReadFile(), ::WriteFile(), ::DeleteFile()
#else
#  include <sys/stat.h>  // ::stat(), ::fstat(), ::lstat(), ::mkdir(), ::fchmod()
#  include <dirent.h>  // ::opendir(), ::closedir()
#  include <fcntl.h>  // ::open()
#  include <unistd.h>  // ::rmdir(), ::close(), ::read(), ::write(), ::unlink()
#  include <stdio.h>  // ::rename()
#endif

namespace Asteria {

    namespace {

#ifdef _WIN32
    // UTF-16 is used on Windows.
    cow_wstring do_translate_winnt_path(const G_string& path)
      {
        cow_wstring wstr;
        wstr.reserve(path.size() + 8);
        // If `path` is an absolute path, translate it to an NT path for long filename support.
        if((path.size() >= 2) && (path[1] == L':')) {
          // Convert lowercase letters to uppercase ones.
          auto letter = path[0] & ~0x20;
          if((L'A' <= letter) && (letter <= L'Z')) {
            wstr.append(L"\\\\\?\\");
          }
        }
        // Convert all characters.
        size_t offset = 0;
        while(offset < path.size()) {
          char32_t cp;
          if(!utf8_decode(cp, path, offset)) {
            ASTERIA_THROW_RUNTIME_ERROR("The path `", path, "` is not a valid UTF-8 string.");
          }
          char16_t str[2];
          char16_t* pos = str;
          utf16_encode(pos, cp);
          wstr.append(str, pos);
        }
        return wstr;
      }

    // Compose a pair of `DWORD`s to form an `uint64_t`.
    constexpr uint64_t do_compose(::DWORD high, ::DWORD low) noexcept
      {
        return (static_cast<uint64_t>(high) << 32) + low;
      }
#endif

    // This is used to close a native file handle when it is out of use.
    struct File_Closer
      {
#ifdef _WIN32
        ::HANDLE null() const noexcept
          {
            return INVALID_HANDLE_VALUE;
          }
        bool is_null(::HANDLE fd) const noexcept
          {
            return fd == INVALID_HANDLE_VALUE;
          }
        void close(::HANDLE fd) const noexcept
          {
            ::CloseHandle(fd);
          }
#else
        constexpr int null() const noexcept
          {
            return -1;
          }
        constexpr bool is_null(int fd) const noexcept
          {
            return fd == -1;
          }
        void close(int fd) const noexcept
          {
            ::close(fd);
          }
#endif
      };

    // This is the smart handle type.
    // It is convertible to a native handle implicitly.
    using File = rocket::unique_handle<decltype(File_Closer().null()), File_Closer>;

    // This is used to close a native directory handle when it is out of use.
    struct Directory_Closer
      {
#ifdef _WIN32
        ::HANDLE null() const noexcept
          {
            return INVALID_HANDLE_VALUE;
          }
        bool is_null(::HANDLE pd) const noexcept
          {
            return pd == INVALID_HANDLE_VALUE;
          }
        void close(::HANDLE pd) const noexcept
          {
            ::FindClose(pd);
          }
#else
        constexpr ::DIR* null() const noexcept
          {
            return nullptr;
          }
        constexpr bool is_null(::DIR* pd) const noexcept
          {
            return pd == nullptr;
          }
        void close(::DIR* pd) const noexcept
          {
            ::closedir(pd);
          }
#endif
      };

    // This is the smart handle type.
    // It is convertible to a native handle implicitly.
    using Directory = rocket::unique_handle<decltype(Directory_Closer().null()), Directory_Closer>;

    }  // namespace

G_string std_filesystem_get_working_directory()
  {
    G_string cwd;
#ifdef _WIN32
    // Get the current directory as UTF-16.
    cow_wstring ucwd(MAX_PATH, L'*');
    auto nreq = ::GetCurrentDirectoryW(static_cast<uint32_t>(ucwd.size()), ucwd.mut_data());
    if(nreq > ucwd.size()) {
      // The buffer was too small.
      ucwd.append(nreq - ucwd.size(), L'*');
      nreq = ::GetCurrentDirectoryW(nreq, ucwd.mut_data());
    }
    // Convert UTF-16 to UTF-8.
    // We only want to stop when a NUL character is encountered.
    cwd.reserve(ucwd.size() + 20);
    auto pos = reinterpret_cast<const char16_t*>(ucwd.c_str());
    for(;;) {
      char32_t cp;
      if(!utf16_decode(cp, pos, SIZE_MAX)) {
        ASTERIA_THROW_RUNTIME_ERROR("The path of the current working directory is not valid UTF-16.");
      }
      if(cp == 0) {
        break;
      }
      utf8_encode(cwd, cp);
    }
#else
    // Get the current directory, resizing the buffer as needed.
    cwd.resize(PATH_MAX);
    while(::getcwd(cwd.mut_data(), cwd.size()) == nullptr) {
      auto err = errno;
      if(err != ERANGE) {
        ASTERIA_THROW_RUNTIME_ERROR("`getcwd()` failed.");
      }
      cwd.append(cwd.size() / 2, '*');
    }
    cwd.erase(cwd.find('\0'));
#endif
    return cwd;
  }

opt<G_object> std_filesystem_get_information(const G_string& path)
  {
    G_object stat;
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    // Open the file or directory.
    File hf(::CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL));
    if(!hf) {
      return rocket::nullopt;
    }
    ::BY_HANDLE_FILE_INFORMATION fbi;
    if(::GetFileInformationByHandle(hf, &fbi) == FALSE) {
      return rocket::nullopt;
    }
    ::FILE_STANDARD_INFO fsi;
    if(::GetFileInformationByHandleEx(hf, FileStandardInfo, &fsi, sizeof(fsi)) == FALSE) {
      return rocket::nullopt;
    }
    // Fill `stat`.
    stat.try_emplace(rocket::sref("i_dev"),
      G_integer(
        fbi.dwVolumeSerialNumber  // unique device id on this machine.
      ));
    stat.try_emplace(rocket::sref("i_file"),
      G_integer(
        do_compose(fbi.nFileIndexHigh, fbi.nFileIndexLow)  // unique file id on this device.
      ));
    stat.try_emplace(rocket::sref("n_ref"),
      G_integer(
        fbi.nNumberOfLinks  // number of hard links to this file.
      ));
    stat.try_emplace(rocket::sref("b_dir"),
      G_boolean(
        fbi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY  // whether this is a directory.
      ));
    stat.try_emplace(rocket::sref("b_sym"),
      G_boolean(
        fbi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT  // whether this is a symbolic link.
      ));
    stat.try_emplace(rocket::sref("n_size"),
      G_integer(
        fsi.EndOfFile.QuadPart  // number of bytes this file contains.
      ));
    stat.try_emplace(rocket::sref("n_ocup"),
      G_integer(
        fsi.AllocationSize.QuadPart  // number of bytes this file occupies.
      ));
    stat.try_emplace(rocket::sref("t_accs"),
      G_integer(
        (do_compose(fbi.ftLastAccessTime.dwHighDateTime, fbi.ftLastAccessTime.dwLowDateTime) - 116444736000000000) / 10000  // timestamp of last access.
      ));
    stat.try_emplace(rocket::sref("t_mod"),
      G_integer(
        (do_compose(fbi.ftLastWriteTime.dwHighDateTime, fbi.ftLastWriteTime.dwLowDateTime) - 116444736000000000) / 10000  // timestamp of last modification.
      ));
#else
    struct ::stat stb;
    if(::lstat(path.c_str(), &stb) != 0) {
      return rocket::nullopt;
    }
    // Fill `stat`.
    stat.try_emplace(rocket::sref("i_dev"),
      G_integer(
        stb.st_dev  // unique device id on this machine.
      ));
    stat.try_emplace(rocket::sref("i_file"),
      G_integer(
        stb.st_ino  // unique file id on this device.
      ));
    stat.try_emplace(rocket::sref("n_ref"),
      G_integer(
        stb.st_nlink  // number of hard links to this file.
      ));
    stat.try_emplace(rocket::sref("b_dir"),
      G_boolean(
        S_ISDIR(stb.st_mode)  // whether this is a directory.
      ));
    stat.try_emplace(rocket::sref("b_sym"),
      G_boolean(
        S_ISLNK(stb.st_mode)  // whether this is a symbolic link.
      ));
    stat.try_emplace(rocket::sref("n_size"),
      G_integer(
        stb.st_size  // number of bytes this file contains.
      ));
    stat.try_emplace(rocket::sref("n_ocup"),
      G_integer(
        static_cast<uint64_t>(stb.st_blocks) * 512  // number of bytes this file occupies.
      ));
    stat.try_emplace(rocket::sref("t_accs"),
      G_integer(
        static_cast<int64_t>(stb.st_atim.tv_sec) * 1000 + stb.st_atim.tv_nsec / 1000000  // timestamp of last access.
      ));
    stat.try_emplace(rocket::sref("t_mod"),
      G_integer(
        static_cast<int64_t>(stb.st_mtim.tv_sec) * 1000 + stb.st_mtim.tv_nsec / 1000000  // timestamp of last modification.
      ));
#endif
    return rocket::move(stat);
  }

bool std_filesystem_move_from(const G_string& path_new, const G_string& path_old)
  {
#ifdef _WIN32
    auto wpath_new = do_translate_winnt_path(path_new);
    auto wpath_old = do_translate_winnt_path(path_old);
    if(::MoveFileExW(wpath_old.c_str(), wpath_new.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
#else
    if(::rename(path_old.c_str(), path_new.c_str()) != 0) {
#endif
      return false;
    }
    return true;
  }

    namespace {

    enum Rmlist
      {
        rmlist_rmdir,     // a subdirectory which should be empty and can be removed
        rmlist_unlink,    // a plain file to be unlinked
        rmlist_expand,    // a subdirectory to be expanded
      };

    // Remove the directory recursively.
#ifdef _WIN32
    opt<G_integer> do_remove_directory_recursive(const cow_wstring& root)
      {
        G_integer count = 0;
        // This is the list of files and directories to be removed.
        cow_bivector<Rmlist, cow_wstring> stack;
        stack.emplace_back(rmlist_expand, root);
        while(!stack.empty()) {
          // Pop an element off the stack.
          auto pair = rocket::move(stack.mut_back());
          stack.pop_back();
          auto& wpath = pair.second;
          // Do something.
          if(pair.first == rmlist_rmdir) {
            // This is an empty directory. Remove it.
            if(::RemoveDirectoryW(wpath.c_str()) == FALSE) {
              return rocket::nullopt;
            }
            count++;
            continue;
          }
          if(pair.first == rmlist_unlink) {
            // This is a plain file. Remove it.
            if(::DeleteFileW(wpath.c_str()) == FALSE) {
              return rocket::nullopt;
            }
            count++;
            continue;
          }
          // This is a subdirectory that has not been expanded. Expand it.
          // Push the directory itself. Since elements are maintained in LIFO order, only when this element
          // is encountered for a second time, will all of its children have been removed.
          stack.emplace_back(rmlist_rmdir, wpath);
          // Append all entries.
          // Make a pattern that will match everything.
          wpath.append(L"\\*");
          // On Windows, `FindFirstFile()` returns the first entry in the directory.
          // Although this is usually '.', it could be a conventional entry when iterating over a root directory i.e. a volume.
          // `FindFirstFile()` can even fail if the root directory is empty, in which case `GetLastError()` returns `ERROR_FILE_NOT_FOUND`.
          ::WIN32_FIND_DATAW next;
          Directory hd(::FindFirstFileW(wpath.c_str(), &next));
          wpath.erase(wpath.size() - 2);
          if(!hd) {
            auto err = ::GetLastError();
            if(err != ERROR_FILE_NOT_FOUND) {
              return rocket::nullopt;
            }
            // The directory is empty.
            // Be advised that this can only happen on a root directory, which cannot be removed.
            continue;
          }
          do {
            // Skip special entries.
            if((std::wcscmp(next.cFileName, L".") == 0) || (std::wcscmp(next.cFileName, L"..") == 0)) {
              continue;
            }
            // Get the name and type of this entry.
            auto child = wpath + L'\\' + next.cFileName;
            bool is_dir = next.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            // Append the entry.
            stack.emplace_back(is_dir ? rmlist_expand : rmlist_unlink, rocket::move(child));
            // Read the next entry.
          } while(::FindNextFileW(hd, &next) != FALSE);
        }
        return rocket::move(count);
      }
#else
    opt<G_integer> do_remove_directory_recursive(const rocket::cow_string& root)
      {
        G_integer count = 0;
        // This is the list of files and directories to be removed.
        cow_bivector<Rmlist, cow_string> stack;
        stack.emplace_back(rmlist_expand, root);
        while(!stack.empty()) {
          // Pop an element off the stack.
          auto pair = rocket::move(stack.mut_back());
          stack.pop_back();
          auto& path = pair.second;
          // Do something.
          if(pair.first == rmlist_rmdir) {
            // This is an empty directory. Remove it.
            if(::rmdir(path.c_str()) != 0) {
              return rocket::nullopt;
            }
            count++;
            continue;
          }
          if(pair.first == rmlist_unlink) {
            // This is a plain file. Remove it.
            if(::unlink(path.c_str()) != 0) {
              return rocket::nullopt;
            }
            count++;
            continue;
          }
          // This is a subdirectory that has not been expanded. Expand it.
          // Push the directory itself. Since elements are maintained in LIFO order, only when this element
          // is encountered for a second time, will all of its children have been removed.
          stack.emplace_back(rmlist_rmdir, path);
          // Append all entries.
          Directory hd(::opendir(path.c_str()));
          if(!hd) {
            return rocket::nullopt;
          }
          // Write entries.
          struct ::dirent* next;
          while((next = ::readdir(hd)) != nullptr) {
            // Skip special entries.
            if((std::strcmp(next->d_name, ".") == 0) || (std::strcmp(next->d_name, "..") == 0)) {
              continue;
            }
            // Get the name and type of this entry.
            auto child = path + '/' + next->d_name;
            bool is_dir;
#  ifdef _DIRENT_HAVE_D_TYPE
            if(ROCKET_EXPECT(next->d_type != DT_UNKNOWN)) {
              // Get the file type if it is available immediately.
              is_dir = next->d_type == DT_DIR;
            }
            else
#  endif
            {
              // If the file type is unknown, ask for it.
              struct ::stat stb;
              if(::lstat(child.c_str(), &stb) != 0) {
                return rocket::nullopt;
              }
              is_dir = S_ISDIR(stb.st_mode);
            }
            // Append the entry.
            stack.emplace_back(is_dir ? rmlist_expand : rmlist_unlink, rocket::move(child));
          }
        }
        return rocket::move(count);
      }
#endif

    }  // namespace

opt<G_integer> std_filesystem_remove_recursive(const G_string& path)
  {
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    // Assume `path` designates a directory and try removing it first.
    if(::RemoveDirectoryW(wpath.c_str()) != FALSE) {
      // An empty directory has been removed.
      // Succeed.
      return G_integer(1);
    }
    auto err = ::GetLastError();
    if(err == ERROR_DIRECTORY) {
      // This is something not a directory.
      if(::DeleteFileW(wpath.c_str()) == FALSE) {
        return rocket::nullopt;
      }
      // Succeed.
      return G_integer(1);
    }
    return do_remove_directory_recursive(wpath);
#else
    if(::rmdir(path.c_str()) == 0) {
      // An empty directory has been removed.
      // Succeed.
      return G_integer(1);
    }
    auto err = errno;
    if(err == ENOTDIR) {
      // This is something not a directory.
      if(::unlink(path.c_str()) != 0) {
        return rocket::nullopt;
      }
      // Succeed.
      return G_integer(1);
    }
    return do_remove_directory_recursive(path);
#endif
  }

opt<G_object> std_filesystem_directory_list(const G_string& path)
  {
    G_object entries;
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    // Make a pattern that will match everything.
    wpath.append(L"\\*");
    // On Windows, `FindFirstFile()` returns the first entry in the directory.
    // Although this is usually '.', it could be a conventional entry when iterating over a root directory i.e. a volume.
    // `FindFirstFile()` can even fail if the root directory is empty, in which case `GetLastError()` returns `ERROR_FILE_NOT_FOUND`.
    ::WIN32_FIND_DATAW next;
    Directory hd(::FindFirstFileW(wpath.c_str(), &next));
    wpath.erase(path.size() - 2);
    if(!hd) {
      auto err = ::GetLastError();
      if(err != ERROR_FILE_NOT_FOUND) {
        return rocket::nullopt;
      }
      // The directory is empty.
      return rocket::move(entries);
    }
    do {
      cow_string name;
      G_object entry;
      // Convert UTF-16 to UTF-8.
      // We only want to stop when a NUL character is encountered.
      auto pos = reinterpret_cast<const char16_t*>(next.cFileName);
      for(;;) {
        char32_t cp;
        if(!utf16_decode(cp, pos, SIZE_MAX)) {
          ASTERIA_THROW_RUNTIME_ERROR("The directory \'", path, "\' contains a file whose name is not valid UTF-16.");
        }
        if(cp == 0) {
          break;
        }
        utf8_encode(name, cp);
      }
      // Get the file type.
      entry.try_emplace(rocket::sref("b_dir"),
        G_boolean(
          next.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
        ));
      entry.try_emplace(rocket::sref("b_sym"),
        G_boolean(
          next.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT
        ));
      // Insert the entry.
      entries.try_emplace(rocket::move(name), rocket::move(entry));
      // Read the next entry.
    } while(::FindNextFileW(hd, &next) != FALSE);
#else
    Directory hd(::opendir(path.c_str()));
    if(!hd) {
      return rocket::nullopt;
    }
    // Write entries.
    struct ::dirent* next;
    while((next = ::readdir(hd)) != nullptr) {
      cow_string name;
      G_object entry;
      // Assume the name is in UTF-8.
      name.assign(next->d_name);
#  ifdef _DIRENT_HAVE_D_TYPE
      if(next->d_type != DT_UNKNOWN) {
        // Get the file type if it is available immediately.
        entry.try_emplace(rocket::sref("b_dir"),
          G_boolean(
            next->d_type == DT_DIR
          ));
        entry.try_emplace(rocket::sref("b_sym"),
          G_boolean(
            next->d_type == DT_LNK
          ));
      }
      else
#  endif
      {
        // If the file type is unknown, ask for it.
        // Compose the path.
        auto child = path + '/' + name;
        // Identify the entry.
        struct ::stat stb;
        if(::lstat(child.c_str(), &stb) != 0) {
          return rocket::nullopt;
        }
        entry.try_emplace(rocket::sref("b_dir"),
          G_boolean(
            S_ISDIR(stb.st_mode)
          ));
        entry.try_emplace(rocket::sref("b_sym"),
          G_boolean(
            S_ISLNK(stb.st_mode)
          ));
      }
      // Insert the entry.
      entries.try_emplace(rocket::move(name), rocket::move(entry));
    }
#endif
    return rocket::move(entries);
  }

opt<G_integer> std_filesystem_directory_create(const G_string& path)
  {
    G_integer count = 1;
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    if(::CreateDirectoryW(wpath.c_str(), nullptr) == FALSE) {
      auto err = ::GetLastError();
      if(err != ERROR_ALREADY_EXISTS) {
        return rocket::nullopt;
      }
      // Fail only if it is not a directory that exists.
      auto attr = ::GetFileAttributesW(wpath.c_str());
      if(attr == INVALID_FILE_ATTRIBUTES) {
        return rocket::nullopt;
      }
      if(!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
#else
    if(::mkdir(path.c_str(), 0777) != 0) {
      auto err = errno;
      if(err != EEXIST) {
        return rocket::nullopt;
      }
      // Fail only if it is not a directory that exists.
      struct ::stat stb;
      if(::stat(path.c_str(), &stb) != 0) {
        return rocket::nullopt;
      }
      if(!S_ISDIR(stb.st_mode)) {
#endif
        ASTERIA_DEBUG_LOG("A file that is not a directory exists on \'", path, "\'.");
        return rocket::nullopt;
      }
      count = 0;
    }
    return rocket::move(count);
  }

opt<G_integer> std_filesystem_directory_remove(const G_string& path)
  {
    G_integer count = 1;
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    if(::RemoveDirectoryW(wpath.c_str()) == FALSE) {
      auto err = ::GetLastError();
      if(err != ERROR_DIR_NOT_EMPTY) {
#else
    if(::rmdir(path.c_str()) != 0) {
      auto err = errno;
      if((err != ENOTEMPTY) && (err != EEXIST)) {
#endif
        return rocket::nullopt;
      }
      count = 0;
    }
    return rocket::move(count);
  }

opt<G_string> std_filesystem_file_read(const G_string& path, const opt<G_integer>& offset, const opt<G_integer>& limit)
  {
    if(offset && (*offset < 0)) {
      ASTERIA_THROW_RUNTIME_ERROR("The file offset shall not be negative (got `", *offset, "`).");
    }
    int64_t roffset = offset.value_or(0);
    int64_t rlimit = rocket::clamp(limit.value_or(INT32_MAX), 0, 1048576);
    G_string data;
    // Open the file for reading.
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    File hf(::CreateFileW(wpath.c_str(), FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if(!hf) {
      return rocket::nullopt;
    }
    // Set the file pointer when an offset is specified, even when it is an explicit zero.
    if(offset) {
      ::LARGE_INTEGER fpos;
      fpos.QuadPart = roffset;
      if(::SetFilePointerEx(hf, fpos, nullptr, FILE_BEGIN) == FALSE) {
        return rocket::nullopt;
      }
    }
    // Don't read too many bytes at a time.
    data.resize(static_cast<size_t>(rlimit));
    // Read data from the offset specified.
    ::DWORD nread;
    if((::ReadFile(hf, data.mut_data(), static_cast<unsigned>(data.size()), &nread, nullptr) == FALSE) && (::GetLastError() != ERROR_HANDLE_EOF)) {
#else
    File hf(::open(path.c_str(), O_RDONLY));
    if(!hf) {
      return rocket::nullopt;
    }
    // Don't read too many bytes at a time.
    data.resize(static_cast<size_t>(rlimit));
    ::ssize_t nread;
    if(offset) {
      // Read data from the offset specified.
      nread = ::pread(hf, data.mut_data(), data.size(), roffset);
    }
    else {
      // Read data from the beginning.
      nread = ::read(hf, data.mut_data(), data.size());
    }
    if(nread < 0) {
#endif
      return rocket::nullopt;
    }
    data.erase(static_cast<size_t>(nread));
    return rocket::move(data);
  }

    namespace {

    inline void do_push_argument(cow_vector<Reference>& args, const Value& value)
      {
        Reference_Root::S_temporary xref = { value };
        args.emplace_back(rocket::move(xref));
      }

    void do_process_block(const Global_Context& global, const G_function& callback, const G_integer& offset, const G_string& data)
      {
        // Set up arguments for the user-defined predictor.
        cow_vector<Reference> args;
        do_push_argument(args, offset);
        do_push_argument(args, data);
        // Call the predictor function, but discard the return value.
        Reference self;
        callback.get().invoke(self, global, rocket::move(args));
      }

    }  // namespace

bool std_filesystem_file_stream(const Global_Context& global, const G_string& path, const G_function& callback, const opt<G_integer>& offset, const opt<G_integer>& limit)
  {
    if(offset && (*offset < 0)) {
      ASTERIA_THROW_RUNTIME_ERROR("The file offset shall not be negative (got `", *offset, "`).");
    }
    int64_t roffset = offset.value_or(0);
    int64_t rlimit = rocket::clamp(limit.value_or(INT32_MAX), 0, 1048576);
    int64_t nremaining = rocket::max(limit.value_or(INT64_MAX), 0);
    G_string data;
    // Open the file for reading.
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    File hf(::CreateFileW(wpath.c_str(), FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if(!hf) {
      return false;
    }
    // Set the file pointer when an offset is specified, even when it is an explicit zero.
    if(offset) {
      ::LARGE_INTEGER fpos;
      fpos.QuadPart = roffset;
      if(::SetFilePointerEx(hf, fpos, nullptr, FILE_BEGIN) == FALSE) {
#else
    File hf(::open(path.c_str(), O_RDONLY));
    if(!hf) {
      return false;
    }
    // Set the file pointer when an offset is specified, even when it is an explicit zero.
    if(offset) {
      if(::lseek64(hf, roffset, SEEK_SET) == -1) {
#endif
        return false;
      }
    }
    for(;;) {
      // Has the read limit been reached?
      if(nremaining <= 0) {
        break;
      }
      // Don't read too many bytes at a time.
      data.resize(static_cast<size_t>(rlimit));
      // Read data from the offset specified.
#ifdef _WIN32
      ::DWORD nread;
      if((::ReadFile(hf, data.mut_data(), static_cast<unsigned>(data.size()), &nread, nullptr) == FALSE) && (::GetLastError() != ERROR_HANDLE_EOF)) {
#else
      ::ssize_t nread = ::read(hf, data.mut_data(), data.size());
      if(nread < 0) {
#endif
        return false;
      }
      if(nread == 0) {
        break;
      }
      data.erase(static_cast<size_t>(nread));
      do_process_block(global, callback, roffset, data);
      // Read the next block.
      nremaining -= nread;
      roffset += nread;
    }
    return true;
  }

bool std_filesystem_file_write(const G_string& path, const G_string& data, const opt<G_integer>& offset)
  {
    if(offset && (*offset < 0)) {
      ASTERIA_THROW_RUNTIME_ERROR("The file offset shall not be negative (got `", *offset, "`).");
    }
    int64_t roffset = offset.value_or(0);
    int64_t nremaining = static_cast<int64_t>(data.size());
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    // Calculate the `dwCreationDisposition` argument.
    // If we are to write from the beginning, truncate the file at creation.
    // This saves us two syscalls to truncate the file below.
    ::DWORD create_disposition = OPEN_ALWAYS;
    if(roffset == 0) {
      create_disposition = CREATE_ALWAYS;
    }
    // Open the file for writing.
    File hf(::CreateFileW(wpath.c_str(), FILE_WRITE_DATA, 0, nullptr, create_disposition, FILE_ATTRIBUTE_NORMAL, NULL));
    if(!hf) {
      return false;
    }
    // Set the file pointer when an offset is specified, even when it is an explicit zero.
    if(offset) {
      // If `roffset` is not zero, truncate the file there.
      // Otherwise, the file will have been truncate at creation.
      ::LARGE_INTEGER fpos;
      fpos.QuadPart = roffset;
      if(::SetFilePointerEx(hf, fpos, nullptr, FILE_BEGIN) == FALSE) {
        return false;
      }
      if(::SetEndOfFile(hf) == FALSE) {
#else
    // Calculate the `flags` argument.
    // If we are to write from the beginning, truncate the file at creation.
    // This saves us two syscalls to truncate the file below.
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    if(roffset == 0) {
      flags |= O_TRUNC;
    }
    // Open the file for writing.
    File hf(::open(path.c_str(), flags, 0666));
    if(!hf) {
      return false;
    }
    // Set the file pointer when an offset is specified, even when it is an explicit zero.
    if(offset) {
      // If `roffset` is not zero, truncate the file there.
      // Otherwise, the file will have been truncate at creation.
      if(::ftruncate64(hf, roffset) != 0) {
#endif
        return false;
      }
    }
    for(;;) {
      // Have all data been written successfully?
      if(nremaining <= 0) {
        break;
      }
      // Write data to the end.
#ifdef _WIN32
      ::DWORD nwritten;
      if(::WriteFile(hf, data.data() + data.size() - nremaining, static_cast<uint32_t>(rocket::min(nremaining, INT32_MAX)), &nwritten, nullptr) == FALSE) {
#else
      ::ssize_t nwritten = ::write(hf, data.data() + data.size() - nremaining, static_cast<size_t>(nremaining));
      if(nwritten < 0) {
#endif
        return false;
      }
      nremaining -= nwritten;
    }
    return true;
  }

bool std_filesystem_file_append(const G_string& path, const G_string& data, const opt<G_boolean>& exclusive)
  {
    int64_t nremaining = static_cast<int64_t>(data.size());
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    // Calculate the `dwCreationDisposition` argument.
    // If we are to write from the beginning, truncate the file at creation.
    // This saves us two syscalls to truncate the file below.
    ::DWORD create_disposition = OPEN_ALWAYS;
    if(exclusive == true) {
      create_disposition = CREATE_NEW;
    }
    // Open the file for writing.
    File hf(::CreateFileW(wpath.c_str(), FILE_APPEND_DATA, 0, nullptr, create_disposition, FILE_ATTRIBUTE_NORMAL, NULL));
#else
    // Calculate the `flags` argument.
    // If we are to write from the beginning, truncate the file at creation.
    // This saves us two syscalls to truncate the file below.
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    if(exclusive == true) {
      flags |= O_EXCL;
    }
    // Open the file for writing.
    File hf(::open(path.c_str(), flags, 0666));
#endif
    if(!hf) {
      return false;
    }
    for(;;) {
      // Have all data been written successfully?
      if(nremaining <= 0) {
        break;
      }
      // Write data to the end.
#ifdef _WIN32
      ::DWORD nwritten;
      if(::WriteFile(hf, data.data() + data.size() - nremaining, static_cast<uint32_t>(rocket::min(nremaining, INT32_MAX)), &nwritten, nullptr) == FALSE) {
#else
      ::ssize_t nwritten = ::write(hf, data.data() + data.size() - nremaining, static_cast<size_t>(nremaining));
      if(nwritten < 0) {
#endif
        return false;
      }
      nremaining -= nwritten;
    }
    return true;
  }

bool std_filesystem_file_copy_from(const G_string& path_new, const G_string& path_old)
  {
#ifdef _WIN32
    auto wpath_new = do_translate_winnt_path(path_new);
    auto wpath_old = do_translate_winnt_path(path_old);
    if(::CopyFileW(wpath_old.c_str(), wpath_new.c_str(), FALSE) == FALSE) {
#else
    // Open the old file.
    File hf_old(::open(path_old.c_str(), O_RDONLY));
    if(!hf_old) {
      return false;
    }
    // Get the file mode and preferred I/O block size.
    struct ::stat stb_old;
    if(::fstat(hf_old, &stb_old) != 0) {
      return false;
    }
    // Create the new file, discarding its contents.
    File hf_new(::open(path_new.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0200));
    if(!hf_new) {
      // If the file cannot be opened, unlink it and try again.
      if((errno == EISDIR) || (::unlink(path_new.c_str()) != 0)) {
        return false;
      }
      hf_new.reset(::open(path_new.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0200));
      if(!hf_new) {
        return false;
      }
    }
    // Allocate the I/O buffer.
    G_string buff;
    buff.resize(static_cast<size_t>(stb_old.st_blksize));
    for(;;) {
      // Read some bytes.
      ::ssize_t nread = ::read(hf_old, buff.mut_data(), buff.size());
      if(nread < 0) {
        return false;
      }
      if(nread == 0) {
        break;
      }
      // Write them all.
      ::ssize_t ntotal = 0;
      do {
        ::ssize_t nwritten = ::write(hf_new, buff.mut_data() + ntotal, static_cast<size_t>(nread - ntotal));
        if(nwritten < 0) {
          return false;
        }
        ntotal += nwritten;
      } while(ntotal < nread);
    }
    // Set the file mode. This must be at the last.
    if(::fchmod(hf_new, stb_old.st_mode) != 0) {
#endif
      return false;
    }
    return true;
  }

bool std_filesystem_file_remove(const G_string& path)
  {
#ifdef _WIN32
    auto wpath = do_translate_winnt_path(path);
    if(::DeleteFileW(wpath.c_str()) == FALSE) {
#else
    if(::unlink(path.c_str()) != 0) {
#endif
      return false;
    }
    return true;
  }

void create_bindings_filesystem(G_object& result, API_Version /*version*/)
  {
    //===================================================================
    // `std.filesystem.get_working_directory()`
    //===================================================================
    result.insert_or_assign(rocket::sref("get_working_directory"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.get_working_directory()`\n"
          "\n"
          "  * Gets the absolute path of the current working directory.\n"
          "\n"
          "  * Returns a `string` containing the path to the current working\n"
          "    directory.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.get_working_directory"), rocket::ref(args));
          // Parse arguments.
          if(reader.start().finish()) {
            // Call the binding function.
            Reference_Root::S_temporary xref = { std_filesystem_get_working_directory() };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.get_information()`
    //===================================================================
    result.insert_or_assign(rocket::sref("get_information"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.get_information(path)`\n"
          "\n"
          "  * Retrieves information of the file or directory designated by\n"
          "    `path`.\n"
          "\n"
          "  * Returns an `object` consisting of the following members (names\n"
          "    that start with `b_` are `boolean` flags; names that start with\n"
          "    `i_` are IDs as `integer`s; names that start with `n_` are\n"
          "    plain `integer`s; names that start with `t_` are timestamps in\n"
          "    UTC as `integer`s):\n"
          "\n"
          "    * `i_dev`   unique device id on this machine.\n"
          "    * `i_file`  unique file id on this device.\n"
          "    * `n_ref`   number of hard links to this file.\n"
          "    * `b_dir`   whether this is a directory.\n"
          "    * `b_sym`   whether this is a symbolic link.\n"
          "    * `n_size`  number of bytes this file contains.\n"
          "    * `n_ocup`  number of bytes this file occupies.\n"
          "    * `t_accs`  timestamp of last access.\n"
          "    * `t_mod`   timestamp of last modification.\n"
          "\n"
          "    On failure, `null` is returned.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.get_information"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_get_information(path);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.remove_recursive()`
    //===================================================================
    result.insert_or_assign(rocket::sref("remove_recursive"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.remove_recursive(path)`\n"
          "\n"
          "  * Removes the file or directory at `path`. If `path` designates\n"
          "    a directory, all of its contents are removed recursively.\n"
          "\n"
          "  * Returns the number of files and directories that have been\n"
          "    successfully removed in total, or `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.remove_recursive"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_remove_recursive(path);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.move_from(path_new, path_old)`
    //===================================================================
    result.insert_or_assign(rocket::sref("move_from"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.move_from(path_new, path_old)`\n"
          "\n"
          "  * Moves (renames) the file or directory at `path_old` to\n"
          "    `path_new`.\n"
          "\n"
          "  * Returns `true` on success, or `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.move_from"), rocket::ref(args));
          // Parse arguments.
          G_string path_new;
          G_string path_old;
          if(reader.start().g(path_new).g(path_old).finish()) {
            // Call the binding function.
            if(!std_filesystem_move_from(path_new, path_old)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.directory_list()`
    //===================================================================
    result.insert_or_assign(rocket::sref("directory_list"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.directory_list(path)`\n"
          "\n"
          "  * Lists the contents of the directory at `path`.\n"
          "\n"
          "  * Returns an `object` containing all entries of the directory at\n"
          "    `path`, including the special subdirectories '.' and '..'. For\n"
          "    each element, its key specifies the filename and the value is\n"
          "    an `object` consisting of the following members (names that\n"
          "    start with `b_` are `boolean` flags; names that start with `i_`\n"
          "    are IDs as `integer`s):\n"
          "\n"
          "    * `b_dir`   whether this is a directory.\n"
          "    * `b_sym`   whether this is a symbolic link.\n"
          "\n"
          "    On failure, `null` is returned.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.directory_list"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_directory_list(path);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.directory_create()`
    //===================================================================
    result.insert_or_assign(rocket::sref("directory_create"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.directory_create(path)`\n"
          "\n"
          "  * Creates a directory at `path`. Its parent directory must exist\n"
          "    and must be accessible. This function does not fail if either\n"
          "    a directory or a symbolic link to a directory already exists on\n"
          "    `path`.\n"
          "\n"
          "  * Returns `1` if a new directory has been created successfully,\n"
          "    `0` if the directory already exists, or `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.directory_create"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_directory_create(path);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.directory_remove()`
    //===================================================================
    result.insert_or_assign(rocket::sref("directory_remove"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.directory_remove(path)`\n"
          "\n"
          "  * Removes the directory at `path`. The directory must be empty.\n"
          "    This function fails if `path` does not designate a directory.\n"
          "\n"
          "  * Returns `1` if the directory has been removed successfully, `0`\n"
          "    if it is not empty, or `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.directory_remove"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_directory_remove(path);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_read()`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_read"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_read(path, [offset], [limit])`\n"
          "\n"
          "  * Reads the file at `path` in binary mode. The read operation\n"
          "    starts from the byte offset that is denoted by `offset` if it\n"
          "    is specified, or from the beginning of the file otherwise. If\n"
          "    `limit` is specified, no more than this number of bytes will be\n"
          "    read.\n"
          "\n"
          "  * Returns the bytes that have been read as a `string`, or `null`\n"
          "    on failure.\n"
          "\n"
          "  * Throws an exception if `offset` is negative.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_read"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          opt<G_integer> offset;
          opt<G_integer> limit;
          if(reader.start().g(path).g(offset).g(limit).finish()) {
            // Call the binding function.
            auto qres = std_filesystem_file_read(path, offset, limit);
            if(!qres) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { rocket::move(*qres) };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_stream()`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_stream"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_stream(path, callback, [offset], [limit])`\n"
          "\n"
          "  * Reads the file at `path` in binary mode and invokes `callback`\n"
          "    with the data read repeatedly. `callback` shall be a binary\n"
          "    `function` whose first argument is the absolute offset of the\n"
          "    data block that has been read, and whose second argument is the\n"
          "    bytes read and stored in a `string`. Data may be transferred in\n"
          "    multiple blocks of variable sizes; the caller shall make no\n"
          "    assumption about the number of times that `callback` will be\n"
          "    called or the size of each individual block. The read operation\n"
          "    starts from the byte offset that is denoted by `offset` if it\n"
          "    is specified, or from the beginning of the file otherwise. If\n"
          "    `limit` is specified, no more than this number of bytes will be\n"
          "    read.\n"
          "\n"
          "  * Returns `true` if all data have been processed successfully, or\n"
          "    `null` on failure.\n"
          "\n"
          "  * Throws an exception if `offset` is negative.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& global, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_stream"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          G_function callback = global.placeholder_function();
          opt<G_integer> offset;
          opt<G_integer> limit;
          if(reader.start().g(path).g(callback).g(offset).g(limit).finish()) {
            // Call the binding function.
            if(!std_filesystem_file_stream(global, path, callback, offset, limit)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_write()`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_write"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_write(path, data, [offset])`\n"
          "\n"
          "  * Writes the file at `path` in binary mode. The write operation\n"
          "    starts from the byte offset that is denoted by `offset` if it\n"
          "    is specified, or from the beginning of the file otherwise. The\n"
          "    file is truncated to this length before the write operation;\n"
          "    any existent contents after the write point are discarded. This\n"
          "    function fails if the data can only be written partially.\n"
          "\n"
          "  * Returns `true` if all data have been written successfully, or\n"
          "    `null` on failure.\n"
          "\n"
          "  * Throws an exception if `offset` is negative.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_write"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          G_string data;
          opt<G_integer> offset;
          if(reader.start().g(path).g(data).g(offset).finish()) {
            // Call the binding function.
            if(!std_filesystem_file_write(path, data, offset)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_append()`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_append"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_append(path, data)`\n"
          "\n"
          "  * Writes the file at `path` in binary mode. The write operation\n"
          "    starts from the end of the file; existent contents of the file\n"
          "    are left intact. If `exclusive` is `true` and a file exists on\n"
          "    `path`, this function fails. This function also fails if the\n"
          "    data can only be written partially.\n"
          "\n"
          "  * Returns `true` if all data have been written successfully, or\n"
          "    `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_append"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          G_string data;
          opt<G_boolean> exclusive;
          if(reader.start().g(path).g(data).g(exclusive).finish()) {
            // Call the binding function.
            if(!std_filesystem_file_append(path, data, exclusive)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_copy_from(path_new, path_old)`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_copy_from"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_copy_from(path_new, path_old)`\n"
          "\n"
          "  * Copies the file `path_old` to `path_new`. If `path_old` is a\n"
          "    symbolic link, it is the target that will be copied, rather\n"
          "    than the symbolic link itself. This function fails if\n"
          "    `path_old` designates a directory.\n"
          "\n"
          "  * Returns `true` on success, or `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_copy_from"), rocket::ref(args));
          // Parse arguments.
          G_string path_new;
          G_string path_old;
          if(reader.start().g(path_new).g(path_old).finish()) {
            // Call the binding function.
            if(!std_filesystem_file_copy_from(path_new, path_old)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // `std.filesystem.file_remove()`
    //===================================================================
    result.insert_or_assign(rocket::sref("file_remove"),
      G_function(rocket::make_refcnt<Simple_Binding_Wrapper>(
        // Description
        rocket::sref(
          "\n"
          "`std.filesystem.file_remove(path)`\n"
          "\n"
          "  * Removes the file at `path`. This function fails if `path`\n"
          "    designates a directory.\n"
          "\n"
          "  * Returns `true` if the file has been removed successfully, or\n"
          "    `null` on failure.\n"
        ),
        // Opaque parameter
        G_null(
          nullptr
        ),
        // Definition
        [](const Value& /*opaque*/, const Global_Context& /*global*/, Reference&& /*self*/, cow_vector<Reference>&& args) -> Reference {
          Argument_Reader reader(rocket::sref("std.filesystem.file_remove"), rocket::ref(args));
          // Parse arguments.
          G_string path;
          if(reader.start().g(path).finish()) {
            // Call the binding function.
            if(!std_filesystem_file_remove(path)) {
              return Reference_Root::S_null();
            }
            Reference_Root::S_temporary xref = { true };
            return rocket::move(xref);
          }
          // Fail.
          reader.throw_no_matching_function_call();
        })
      ));
    //===================================================================
    // End of `std.filesystem`
    //===================================================================
  }

}  // namespace Asteria
