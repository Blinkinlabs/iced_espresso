#!/usr/bin/python3

import os
import shutil
import string
import git

# The file's contents will be added to the .rodata section in flash, and are available via symbol names as follows:
#
# extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
# extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");
#
# The names are generated from the full name of the file, as given in
# COMPONENT_EMBED_FILES. Characters /, ., etc. are replaced with underscores.
# The _binary prefix in the symbol name is added by objcopy and is the same
# for both text and binary files.

# Output a symbol table for all file entries
#
# typedef struct {
#     const char *name;
#     const uint8_t *start;
#     const uint8_t *end;
# }rom_file_entry_t;
def build_rom_component(component_name, files, version):
    targetdir = 'components/{}/'.format(component_name)
    
    # Remove the target directory
    if(os.path.isdir(targetdir)):
        shutil.rmtree(targetdir)

    # and make it again
    os.mkdir(targetdir)
    os.mkdir('{}include/'.format(targetdir))

    # CMake build directives
    with open('{}CMakeLists.txt'.format(targetdir), 'w') as cmake_file:
        cmake_file.write('set(embed')
        for f in files:
            cmake_file.write('\n    "../../{}"'.format(f))
        cmake_file.write(')\n')
    
        cmake_file.write('\n')
        cmake_file.write('idf_component_register(\n')
        cmake_file.write('    SRCS "{}.c"\n'.format(component_name))
        cmake_file.write('    INCLUDE_DIRS "include"\n')
        cmake_file.write('    EMBED_FILES "${embed}")\n')

    # Header for project inclusion
    with open('{}include/{}.h'.format(targetdir,component_name), 'w') as file_table:
        file_table.write('#pragma once\n')
        file_table.write('\n')
        file_table.write('#define {}_VERSION "{}"\n'.format(component_name.upper(), version))
        file_table.write('\n')
        file_table.write('//! @brief {} file table entry\n'.format(component_name))
        file_table.write('typedef struct {\n')
        file_table.write('    const char *name;         //!< File name\n')
        file_table.write('    const uint8_t *start;     //!< Pointer to the start of the file in ROM\n')
        file_table.write('    const uint8_t *end;       //!< Pointer to the end of the file in ROM\n')
        file_table.write('}} {}_entry_t;\n'.format(component_name))
        file_table.write('\n')
        file_table.write('#define {}_ENTRY_COUNT {}     //!< Number of entries in the {} table\n'.format(component_name.upper(), len(files), component_name))
        file_table.write('\n')
        file_table.write('//! @brief {} entry table \n'.format(component_name))
        file_table.write('extern const {}_entry_t {}_entries[{}_ENTRY_COUNT];\n'.format(component_name, component_name, component_name.upper()))
    
    # C source with ROM file map
    with open('{}{}.c'.format(targetdir, component_name), 'w') as file_table:
        file_table.write('#include <stdint.h>\n')
        file_table.write('#include "{}.h"\n'.format(component_name))
        file_table.write('\n')
   
        for f in files:
            filename = os.path.basename(f)
            filename_flattened = filename.replace('.','_')
    
            file_table.write('extern const unsigned char {0}_start asm("_binary_{0}_start");\n'.format(filename_flattened))
            file_table.write('extern const unsigned char {0}_end asm("_binary_{0}_end");\n'.format(filename_flattened))
    
        file_table.write('\n')
    
        file_table.write('const {}_entry_t {}_entries[{}_ENTRY_COUNT] = {{\n'.format(component_name,component_name,component_name.upper()))
    
        for f in files:
            filename = os.path.basename(f)
            filename_flattened = filename.replace('.','_')

            file_table.write('    {\n') 
            file_table.write('        .name = "{}",\n'.format(filename)) 
            file_table.write('        .start = &{}_start,\n'.format(filename_flattened)) 
            file_table.write('        .end = &{}_end,\n'.format(filename_flattened)) 
            file_table.write('    },\n')
    
        file_table.write('};\n')

    # Output a memory usage summary
    print("{:8} {}".format("size", "filename"))
    size_total = 0
    for f in files:
        filename = os.path.basename(f)

        size = os.stat(f).st_size

        print("{:8} {}".format(size, filename))
        size_total += size
    
    print("Total size: {}".format(size_total))

def git_version(directory):
    print(directory)
    repo = git.Repo(directory, search_parent_directories=True)
    assert not repo.bare
    version = repo.git.describe('--tags', '--dirty', '--broken')
    print(version)
    return version

component_name = 'fpga_bin'


# Extract version information
version = git_version('../')

# FPGA binaries to include
files = [
    'fpga/top.bin'
]

build_rom_component(component_name, files, version)
