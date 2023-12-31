/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "modprobe.h"

class UniqueFd {
public:
    UniqueFd(const std::string& path) {
        fd = TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    }

    ~UniqueFd() {
        if (fd != -1) {
            close(fd);
        }
    }

    // Disallow copying and assignment.
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    // Allow moving.
    UniqueFd(UniqueFd&& other) noexcept : fd(other.fd) {
        other.fd = -1;
    }

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd != -1) {
                close(fd);
            }
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }

    explicit operator bool() const {
        return fd != -1;
    }

    int get() const {
        return fd;
    }

private:
    int fd = -1;
};

bool Modprobe::Insmod(const std::string& path_name, const std::string& parameters) {
    UniqueFd fd(path_name);

    if (fd.get() == -1) {
        return false;
    }

    auto canonical_name = MakeCanonical(path_name);
    std::string options = "";
    auto options_iter = module_options_.find(canonical_name);
    if (options_iter != module_options_.end()) {
        options = options_iter->second;
    }
    if (!parameters.empty()) {
        options = options + " " + parameters;
    }

    std::cout << "Loading module " << path_name << " with args '" << options << "'" << std::endl;
    int ret = syscall(__NR_finit_module, fd.get(), options.c_str(), 0);
    if (ret != 0) {
        if (errno == EEXIST) {
            // Module already loaded
            std::lock_guard guard(module_loaded_lock_);
            module_loaded_paths_.emplace(path_name);
            module_loaded_.emplace(canonical_name);
            return true;
        }
        std::cout << "Failed to insmod '" << path_name << "' with args '" << options << "'" << std::endl;
        return false;
    }

    std::cout << "Loaded kernel module " << path_name << std::endl;
    std::lock_guard guard(module_loaded_lock_);
    module_loaded_paths_.emplace(path_name);
    module_loaded_.emplace(canonical_name);
    module_count_++;
    return true;
}

bool Modprobe::Rmmod(const std::string& module_name) {
    auto canonical_name = MakeCanonical(module_name);
    int ret = syscall(__NR_delete_module, canonical_name.c_str(), O_NONBLOCK);
    if (ret != 0) {
        std::cout << "Failed to remove module '" << module_name << "'" << std::endl;
        return false;
    }
    std::lock_guard guard(module_loaded_lock_);
    module_loaded_.erase(canonical_name);
    return true;
}

bool Modprobe::ModuleExists(const std::string& module_name) {
    struct stat fileStat {};
    if (blocklist_enabled && module_blocklist_.count(module_name)) {
        std::cout << "module " << module_name << " is blocklisted" << std::endl;
        return false;
    }
    auto deps = GetDependencies(module_name);
    if (deps.empty()) {
        // missing deps can happen in the case of an alias
        return false;
    }
    if (stat(deps.front().c_str(), &fileStat)) {
        std::cout << "module " << module_name << " can't be loaded; can't access " << deps.front() << std::endl;
        return false;
    }
    if (!S_ISREG(fileStat.st_mode)) {
        std::cout << "module " << module_name << " is not a regular file" << std::endl;
        return false;
    }
    return true;
}
