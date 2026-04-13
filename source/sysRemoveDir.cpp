#include <string>
#include <cstring>

#ifdef _WIN32
#include "wincompat.h"
#include <stdio.h>

// Windows: recursive directory removal using FindFirstFile/FindNextFile
static void sysRemoveDirRecursive(const std::string &dirPath) {
    std::string searchPath = dirPath + "\\*";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
            continue;

        std::string fullPath = dirPath + "\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            sysRemoveDirRecursive(fullPath);
        } else {
            DeleteFileA(fullPath.c_str());
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    RemoveDirectoryA(dirPath.c_str());
}

void sysRemoveDir(std::string dirName) {
    sysRemoveDirRecursive(dirName);
};

#else

//#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <unistd.h>

int removeFileOrDir(const char *fpath,const struct stat *sb, int typeflag, struct FTW *ftwbuf) {

    {//to avoid unused variable warning
        (void) sb;
        (void) ftwbuf;
    };

    if (typeflag==FTW_F) {//file
        remove(fpath);
    } else if (typeflag==FTW_DP) {//dir
        rmdir(fpath);
    } else {//something went wrong, stop the removal
        return -1;
    };
    return 0;
};


void sysRemoveDir(std::string dirName) {//remove directory and all its contents
    int nftwFlag=FTW_DEPTH;
    nftw(dirName.c_str(), removeFileOrDir, 100, nftwFlag);
};

#endif
