/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
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

#define MODULE_BASE_DIR "/lib/modules"

std::string GetPageSizeSuffix() {
    static const size_t page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size <= 4096) {
        return "";
    }

    std::ostringstream oss;
    oss << "_" << (page_size / 1024) << "k";

    return oss.str();
}

std::string GetModuleLoadList(const std::string& dir_path) {
    std::string module_load_file = "modules.load.recovery";
    struct stat fileStat {};

    std::string recovery_load_path = dir_path + "/" + module_load_file;
    // Check if the .recovery file exists
    if (stat(recovery_load_path.c_str(), &fileStat)) {
        // If the .recovery file doesn't exist, use modules.load
        module_load_file = "modules.load";
    }

    return module_load_file;
}

bool LoadKernelModules(int& modules_loaded) {
    struct utsname uts {};
    if (uname(&uts)) {
        std::cout << "Failed to get kernel version." << std::endl;
    }
    int major = 0, minor = 0;
    if (sscanf(uts.release, "%d.%d", &major, &minor) != 2) {
        std::cout << "Failed to parse kernel version " << uts.release << std::endl;
    }

    std::unique_ptr<DIR, decltype(&closedir)> base_dir(opendir(MODULE_BASE_DIR), closedir);
    if (!base_dir) {
        std::cout << "Unable to open /lib/modules, skipping module loading." << std::endl;
        return true;
    }
    dirent* entry = nullptr;
    std::vector<std::string> module_dirs;
    const auto page_size_suffix = GetPageSizeSuffix();
    const std::string release_specific_module_dir = uts.release + page_size_suffix;
    while ((entry = readdir(base_dir.get()))) {
        if (entry->d_type != DT_DIR) {
            continue;
        }
        if (entry->d_name == release_specific_module_dir) {
            std::cout << "Release specific kernel module dir " << release_specific_module_dir
                      << " found, loading modules from here with no fallbacks." << std::endl;
            module_dirs.clear();
            module_dirs.emplace_back(entry->d_name);
            break;
        }
        // Is a directory does not have page size suffix, it does not mean this directory is for 4K
        // kernels. Certain 16K kernel builds put all modules in /lib/modules/`uname -r` without any
        // suffix. Therefore, only ignore a directory if it has _16k/_64k suffix and the suffix does
        // not match system page size.
        const auto dir_page_size_suffix = GetPageSizeSuffix();
        if (!dir_page_size_suffix.empty() && dir_page_size_suffix != page_size_suffix) {
            continue;
        }
        int dir_major = 0, dir_minor = 0;
        if (sscanf(entry->d_name, "%d.%d", &dir_major, &dir_minor) != 2 || dir_major != major ||
            dir_minor != minor) {
            continue;
        }
        module_dirs.emplace_back(entry->d_name);
    }

    // Sort the directories so they are iterated over during module loading
    // in a consistent order. Alphabetical sorting is fine here because the
    // kernel version at the beginning of the directory name must match the
    // current kernel version, so the sort only applies to a label that
    // follows the kernel version, for example /lib/modules/5.4 vs.
    // /lib/modules/5.4-gki.
    std::sort(module_dirs.begin(), module_dirs.end());

    for (const auto& module_dir : module_dirs) {
        std::string dir_path = MODULE_BASE_DIR "/";
        dir_path.append(module_dir);
        Modprobe m({dir_path}, GetModuleLoadList(dir_path));
        bool retval = m.LoadListedModules();
        modules_loaded = m.GetModuleCount();
        if (modules_loaded > 0) {
            std::cout << "Loaded " << modules_loaded << " modules from " << dir_path << std::endl;
            return retval;
        }
    }

    Modprobe m({MODULE_BASE_DIR}, GetModuleLoadList(MODULE_BASE_DIR));
    bool retval = m.LoadModulesParallel(std::thread::hardware_concurrency());

    modules_loaded = m.GetModuleCount();
    if (modules_loaded > 0) {
        std::cout << "Loaded " << modules_loaded << " modules from " << MODULE_BASE_DIR << std::endl;
        return retval;
    }
    return true;
}

int main() {
    int modules_loaded = 0;

    LoadKernelModules(modules_loaded);
    std::cout << "Total modules loaded: " << modules_loaded << std::endl;

    return 0;
}
