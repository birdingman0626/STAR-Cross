#ifndef STAR_VERSION_H
#define STAR_VERSION_H

// Defined in the build-generated STARVersionInfo.cpp, which is refreshed on
// every build, so the reported commit follows the checked-out revision.
extern const char* const starVersionString; // "STAR-Cross <short commit>"
extern const char* const starGitCommit;     // "<short commit>" or "unknown"

#define STAR_VERSION starVersionString
#define GIT_BRANCH_COMMIT_DIFF starGitCommit

#endif
