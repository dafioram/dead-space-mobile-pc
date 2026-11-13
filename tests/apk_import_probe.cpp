#include "apk.h"

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    return apk::import(argv[1], argv[2]) ? 0 : 1;
}
