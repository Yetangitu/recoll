#ifndef lint
static char rcsid[] = "@(#$Id: pathut.cpp,v 1.2 2004-12-14 17:54:16 dockes Exp $ (C) 2004 J.F.Dockes";
#endif

#ifndef TEST_PATHUT

#include <pwd.h>

#include "pathut.h"

std::string path_getfather(const std::string &s) {
    std::string father = s;

    // ??
    if (father.empty())
	return "./";

    if (father[father.length() - 1] == '/') {
	// Input ends with /. Strip it, handle special case for root
	if (father.length() == 1)
	    return father;
	father.erase(father.length()-1);
    }

    std::string::size_type slp = father.rfind('/');
    if (slp == std::string::npos)
	return "./";

    father.erase(slp);
    path_catslash(father);
    return father;
}

std::string path_getsimple(const std::string &s) {
    std::string simple = s;

    if (simple.empty())
	return simple;

    std::string::size_type slp = simple.rfind('/');
    if (slp == std::string::npos)
	return simple;

    simple.erase(0, slp+1);
    return simple;
}

std::string path_home()
{
    uid_t uid = getuid();

    struct passwd *entry = getpwuid(uid);
    if (entry == 0)
	return "/";

    std::string homedir = entry->pw_dir;
    path_catslash(homedir);
    return homedir;
}

#else // TEST_PATHUT

#include <iostream>
using namespace std;

#include "pathut.h"

const char *tstvec[] = {"", "/", "/dir", "/dir/", "/dir1/dir2",
			 "/dir1/dir2",
			"./dir", "./dir1/", "dir", "../dir", "/dir/toto.c",
			"/dir/.c",
};

int main(int argc, const char **argv)
{

    for (int i = 0;i < sizeof(tstvec) / sizeof(char *); i++) {
	cout << tstvec[i] << " FATHER " << path_getfather(tstvec[i]) << endl;
    }
    for (int i = 0;i < sizeof(tstvec) / sizeof(char *); i++) {
	cout << tstvec[i] << " SIMPLE " << path_getsimple(tstvec[i]) << endl;
    }
    return 0;
}

#endif // TEST_PATHUT

