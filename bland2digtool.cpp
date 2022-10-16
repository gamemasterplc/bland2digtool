#define _CRT_SECURE_NO_WARNINGS //Shut up Visual Studio
#include <stdio.h>
#include <stdint.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <unordered_map>
#include <string>
#include <iostream>
#include <algorithm>
#include "tinyxml2.h"

#define SECTOR_SIZE 2048
#define FILE_ALIGNMENT 32
#define MAX_ITERS_HEADER_END_FIND 2048

struct FileInfo {
    std::string name;
    uint32_t id;
    uint32_t size; //Bytes
    uint32_t offset; //Bytes
    uint32_t type;
};

struct DirInfo {
    std::string name;
    uint32_t id;
    uint32_t offset; //Sectors
    uint32_t size; //Sectors
    std::vector<FileInfo> files;
};

std::unordered_map<uint32_t, std::string> type_extension_map = {
    {1, ".tpl"}, //Images
    {2, ".tpl"}, //Images
    {3, ".tpl"}, //Images
    {4, ".tpl"}, //Images
    {5, ".tpl"}, //Images
    {6, ".tpl"}, //Images
    {64, ".spr"}, //Sprite
    {65, ".map"}, //Tilemap
    {66, ".col"}, //Collision Map
    {67, ".dlg"}, //Dialog boxes
};

std::vector<DirInfo> dir_list;
std::unordered_map<uint32_t, uint32_t> id_dir_index_map;
size_t dig_filesize;

uint32_t ReadU32(FILE *file)
{
    uint8_t temp[4];
    fread(&temp, 1, 4, file);
    return (temp[0] << 24) | (temp[1] << 16) | (temp[2] << 8) | temp[3];
}

void WriteU32(FILE *file, uint32_t value)
{
    uint8_t temp[4];
    temp[0] = value >> 24;
    temp[1] = (value >> 16) & 0xFF;
    temp[2] = (value >> 8) & 0xFF;
    temp[3] = value & 0xFF;
    fwrite(temp, 4, 1, file);
}

bool MakeDirectory(std::string dir)
{
    int ret;
    #if defined(_WIN32)
    ret = _mkdir(dir.c_str());
    #else 
    ret = mkdir(dir.c_str(), 0777); // notice that 777 is different than 0777
    #endif]
    return ret != -1 || errno == EEXIST;
}

std::string GetFileTypeExtension(uint32_t type)
{
    if (!type_extension_map.count(type)) {
        //Return .bin for unknown file types
        return ".bin";
    }
    return type_extension_map[type];
}

void XMLCheck(tinyxml2::XMLError error)
{
    if (error != tinyxml2::XML_SUCCESS) {
        std::cout << "tinyxml2 error " << tinyxml2::XMLDocument::ErrorIDToName(error) << std::endl;
        exit(1);
    }
}

size_t GetNumHeaderSectors(FILE *file)
{
    size_t num_iters = 0;
    for (size_t i = 0; i < MAX_ITERS_HEADER_END_FIND; i++) {
        uint32_t offset = ReadU32(file);
        uint32_t size = ReadU32(file);
        uint32_t num_files = ReadU32(file);
        if (num_files != 0) {
            //Return offset of first directory with non-zero file count
            return offset;
        }
    }
    std::cout << "Could not find header sector." << std::endl;
    fclose(file);
    exit(1);
}

bool VerifyDIGFile(FILE *file, size_t num_header_sectors)
{
    fseek(file, 0, SEEK_END);
    size_t file_len = ftell(file);
    fseek(file, 0, SEEK_SET);
    for (size_t i = 0; i < (num_header_sectors * SECTOR_SIZE) / 12; i++) {
        //Read directory info
        uint32_t offset = ReadU32(file);
        uint32_t size = ReadU32(file);
        uint32_t num_files = ReadU32(file);
        if (num_files == 0) {
            //Skip 0-file directories
            continue;
        }
        //Invalid directories exceed file boundaries
        if ((offset + size) * SECTOR_SIZE > file_len) {
            return false;
        }
    }
    //Valid DIG File
    return true;
}
void ReadDIGDirectories(FILE *file, size_t num_header_sectors)
{
    //Populate directory list
    for (size_t i = 0; i < (num_header_sectors * SECTOR_SIZE) / 12; i++) {
        //Read directory info
        uint32_t offset = ReadU32(file);
        uint32_t size = ReadU32(file);
        uint32_t num_files = ReadU32(file);
        if (num_files == 0) {
            //Do not insert directories with zero files
            continue;
        }
        DirInfo dir;
        //Populate directory info
        dir.name = "dir" + std::to_string(i);
        dir.id = i;
        dir.offset = offset;
        dir.size = size;
        //Read directory file list
        long prev_offset = ftell(file);
        fseek(file, offset * SECTOR_SIZE, SEEK_SET);
        for (size_t j = 0; j < num_files; j++) {
            FileInfo file_info;
            //Read file info
            file_info.id = ReadU32(file);
            file_info.size = ReadU32(file);
            file_info.offset = ReadU32(file);
            file_info.type = ReadU32(file);
            file_info.name = "file" + std::to_string(file_info.id) + GetFileTypeExtension(file_info.type);
            dir.files.push_back(file_info);
        }
        //Go back to directory info offset
        fseek(file, prev_offset, SEEK_SET);
        dir_list.push_back(dir);
    }
}

void DumpDIGFiles(FILE *file, std::string root_dir)
{
    //Create root directory
    if (!MakeDirectory(root_dir)) {
        std::cout << "Failed to create directory " << root_dir << "." << std::endl;
        fclose(file);
        exit(1);
    }
    //Dump directories
    for (size_t i = 0; i < dir_list.size(); i++) {
        std::string new_dir_name = root_dir + dir_list[i].name + "/";
        if (!MakeDirectory(new_dir_name)) {
            std::cout << "Failed to create directory " << new_dir_name << "." << std::endl;
            fclose(file);
            exit(1);
        }
        //Dump files in directory
        std::cout << "Dumping directory " << dir_list[i].name << std::endl;
        for (size_t j = 0; j < dir_list[i].files.size(); j++) {
            //Get filename
            std::string out_name = new_dir_name + dir_list[i].files[j].name;
            //Try to create file
            FILE *temp_file = fopen(out_name.c_str(), "wb");
            if (!temp_file) {
                std::cout << "Failed to create file " << out_name << "." << std::endl;
                fclose(file);
                exit(1);
            }
            //Dump this file
            uint32_t file_offset = (dir_list[i].offset * SECTOR_SIZE) + dir_list[i].files[j].offset;
            uint32_t file_size = dir_list[i].files[j].size;
            fseek(file, file_offset, SEEK_SET);
            uint8_t *temp_buf = new uint8_t[file_size];
            fread(temp_buf, 1, file_size, file);
            fwrite(temp_buf, 1, file_size, temp_file);
            fclose(temp_file);
            delete[] temp_buf;
        }
    }
}

void CreateListing(std::string list_name, std::string name)
{
    //Create listing
    tinyxml2::XMLDocument document;
    tinyxml2::XMLElement *root = document.NewElement("filelist");
    document.InsertFirstChild(root);
    //Add directories to listing
    for (size_t i = 0; i < dir_list.size(); i++) {
        tinyxml2::XMLElement *dir_element = document.NewElement("directory");
        dir_element->SetAttribute("id", dir_list[i].id);
        dir_element->SetAttribute("name", dir_list[i].name.c_str());
        //Add files to listing
        for (size_t j = 0; j < dir_list[i].files.size(); j++) {
            tinyxml2::XMLElement *file_element = document.NewElement("file");
            file_element->SetAttribute("id", dir_list[i].files[j].id);
            file_element->SetAttribute("type", dir_list[i].files[j].type);
            file_element->SetAttribute("name", dir_list[i].files[j].name.c_str());
            dir_element->InsertEndChild(file_element);
        }
        root->InsertEndChild(dir_element);
    }
    //Try to save listing file
    XMLCheck(document.SaveFile(list_name.c_str()));
}

void DumpDIG(std::string in_file)
{
    std::string dir_name = in_file.substr(0, in_file.find_last_of("\\/") + 1);
    std::string filename = in_file.substr(in_file.find_last_of("\\/")+1);
    std::string name = filename.substr(0, filename.find_last_of("."));
    std::string list_name = dir_name + name + ".lst";
    //Open DIG file
    FILE *dig_file = fopen(in_file.c_str(), "rb");
    if (!dig_file) {
        std::cout << "Failed to open " << in_file << " for reading." << std::endl;
        exit(1);
    }
    size_t num_header_sectors = GetNumHeaderSectors(dig_file);
    if (!VerifyDIGFile(dig_file, num_header_sectors)) {
        std::cout << "DIG file is invalid" << std::endl;
        fclose(dig_file);
        exit(1);
    }
    //Reset seek for reading DIG file
    fseek(dig_file, 0, SEEK_SET);
    //Read DIG file
    ReadDIGDirectories(dig_file, num_header_sectors);
    DumpDIGFiles(dig_file, dir_name + name + "/");
    fclose(dig_file);
    CreateListing(list_name, name);
}

void ParseDirectories(tinyxml2::XMLElement *root_node)
{
    //Loop over all directories
    tinyxml2::XMLElement *dir_node = root_node->FirstChildElement("directory");
    while (dir_node) {
        tinyxml2::XMLElement *file_node = dir_node->FirstChildElement("file");
        if (!file_node) {
            //Skip adding directory if no files exist
            dir_node = dir_node->NextSiblingElement("directory");
            continue;
        }
        DirInfo dir;
        const char *dir_name_value;
        dir.offset = 0;
        dir.size = 0;
        //Get directory info
        XMLCheck(dir_node->QueryAttribute("id", &dir.id));
        XMLCheck(dir_node->QueryAttribute("name", &dir_name_value));
        dir.name = dir_name_value;
        //Loop over all files in directory
        while (file_node) {
            FileInfo file;
            //Get file info
            const char *file_name_value;
            XMLCheck(file_node->QueryAttribute("id", &file.id));
            XMLCheck(file_node->QueryAttribute("type", &file.type));
            XMLCheck(file_node->QueryAttribute("name", &file_name_value));
            file.name = file_name_value;
            file.offset = 0;
            file.size = 0;
            //Add file info to directory
            dir.files.push_back(file);
            file_node = file_node->NextSiblingElement("file");
        }
        id_dir_index_map[dir.id] = dir_list.size(); //Add directory ID mapping
        //Add directory info
        dir_list.push_back(dir);
        dir_node = dir_node->NextSiblingElement("directory");
    }
}

void CalcFileRanges(std::string file_dir)
{
    for (size_t i = 0; i < dir_list.size(); i++) {
        //Align first file offset to 32 bytes
        size_t file_ofs = ((dir_list[i].files.size() * 16) + FILE_ALIGNMENT - 1) & ~(FILE_ALIGNMENT - 1);
        for (size_t j = 0; j < dir_list[i].files.size(); j++) {
            uint32_t size;
            //Open file
            std::string path = file_dir + dir_list[i].name + "/" + dir_list[i].files[j].name;
            FILE *file = fopen(path.c_str(), "rb");
            if (!file) {
                std::cout << "Failed to open file " << path << " for reading." << std::endl;
                exit(1);
            }
            //Get size of file
            fseek(file, 0, SEEK_END);
            dir_list[i].files[j].size = size = ftell(file);
            fclose(file);
            dir_list[i].files[j].offset = file_ofs; //Record file offset
            //Align next file offset to 32 bytes
            file_ofs += (size + FILE_ALIGNMENT - 1) & ~(FILE_ALIGNMENT - 1);
        }
    }
}

uint32_t GetNumDirs()
{
    size_t max_id = 0;
    for (size_t i = 0; i < dir_list.size(); i++) {
        if (dir_list[i].id > max_id) {
            max_id = dir_list[i].id;
        }
    }
    return max_id + 1;
}

uint32_t CalcFirstDataSector()
{
    uint32_t num_dirs = GetNumDirs();
    return (((num_dirs) * 12) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
}

uint32_t CalcDirSectorCount(size_t dir_index)
{
    //Get offset and size of last file
    uint32_t num_files = dir_list[dir_index].files.size() - 1;
    uint32_t last_file_ofs = dir_list[dir_index].files[num_files].offset;
    uint32_t last_file_size = dir_list[dir_index].files[num_files].size;
    //Directory ends after last file
    uint32_t dir_end_ofs = last_file_ofs + last_file_size;
    //Pad to next sector
    return (dir_end_ofs + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
}

void CalcDirRanges()
{
    uint32_t sector = CalcFirstDataSector();
    for (size_t i = 0; i < dir_list.size(); i++) {
        uint32_t num_sectors = CalcDirSectorCount(i);
        //Record directory ranges
        dir_list[i].offset = sector;
        dir_list[i].size = num_sectors;
        sector += num_sectors; //Place directories back to back
    }
}

void WriteDirectoryList(FILE *file)
{
    uint32_t num_dirs = GetNumDirs();
    for (size_t i = 0; i < num_dirs; i++) {
        if (id_dir_index_map.count(i) == 0) {
            //Directory not in file
            WriteU32(file, 0);
            WriteU32(file, 0);
            WriteU32(file, 0);
        } else {
            //Write directory info to file
            uint32_t mapped_dir = id_dir_index_map[i];
            WriteU32(file, dir_list[mapped_dir].offset);
            WriteU32(file, dir_list[mapped_dir].size);
            WriteU32(file, dir_list[mapped_dir].files.size());
        }
    }
}

void WriteDIGFiles(FILE *file, std::string file_dir)
{
    for (size_t i = 0; i < dir_list.size(); i++) {
        uint32_t base_ofs = dir_list[i].offset * SECTOR_SIZE;
        fseek(file, base_ofs, SEEK_SET);
        std::cout << "Writing directory ID " << dir_list[i].id << "." << std::endl;
        for (size_t j = 0; j < dir_list[i].files.size(); j++) {
            //Write file info
            WriteU32(file, dir_list[i].files[j].id);
            WriteU32(file, dir_list[i].files[j].size);
            WriteU32(file, dir_list[i].files[j].offset);
            WriteU32(file, dir_list[i].files[j].type);
            //Read file info for copy
            uint32_t file_ofs = base_ofs + dir_list[i].files[j].offset;
            uint32_t file_size = dir_list[i].files[j].size;
            uint8_t *file_buffer = new uint8_t[file_size];
            std::string path = file_dir + dir_list[i].name + "/" + dir_list[i].files[j].name;
            FILE *data_file = fopen(path.c_str(), "rb");
            //Copy file to DIG file
            size_t old_ofs = ftell(file);
            fseek(file, file_ofs, SEEK_SET);
            fread(file_buffer, 1, file_size, data_file);
            fwrite(file_buffer, 1, file_size, file);
            //Seek back to file info list
            fseek(file, old_ofs, SEEK_SET);
            //Clean up after file copy
            delete[] file_buffer;
            fclose(data_file);
        }
    }
}
void GenerateDIG(std::string in_file)
{
    //Generated strings from path
    std::string list_dir = in_file.substr(0, in_file.find_last_of("\\/") + 1);
    std::string filename = in_file.substr(in_file.find_last_of("\\/") + 1);
    std::string name = filename.substr(0, filename.find_last_of("."));
    std::string out_filename = list_dir + name + ".dig";
    std::string file_dir = list_dir + name + "/";
    tinyxml2::XMLDocument document;
    XMLCheck(document.LoadFile(in_file.c_str()));
    tinyxml2::XMLElement *root = document.FirstChildElement();
    if (!root) {
        std::cout << "Invalid listing file." << std::endl;
        exit(1);
    }
    ParseDirectories(root);
    //DIG file must have directories 
    if (dir_list.size() == 0) {
        std::cout << "No directories in listing file." << std::endl;
        exit(1);
    }
    //Prepare for writing file
    CalcFileRanges(file_dir);
    CalcDirRanges();
    FILE *file = fopen(out_filename.c_str(), "wb");
    if (!file) {
        std::cout << "Could not open " << out_filename << " for writing." << std::endl;
        exit(1);
    }
    //Do DIG file write
    WriteDirectoryList(file);
    WriteDIGFiles(file, file_dir);
    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        //Print usage
        std::cout << "Usage: " << argv[0] << " {-g|-d} in" << std::endl;
        std::cout << "-g generates a DIG file from the listing file (in)" << std::endl;
        std::cout << "-d dumps a DIG file (in) into a directory named after the input DIG file" << std::endl;
        std::cout << "A listing file will also be produced when the -d option is used." << std::endl;
        return 1;
    }
    std::string option = argv[1];
    std::string in_name = argv[2];
    //Handle options
    if (option == "-d") {
        DumpDIG(in_name);
    } else if (option == "-g") {
        GenerateDIG(in_name);
    } else {
        std::cout << "Invalid option " << option << "." << std::endl;
        return 1;
    }
    //Program succeeded
    return 0;
}