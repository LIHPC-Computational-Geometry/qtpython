#include <iostream>
#include <QtPython3/QtPython.h>

int main() {
    TkUtil::Version v = QtPython::getVersion();
    std::cout << "Version " << v.getVersion() << std::endl;
    return 0;
}
