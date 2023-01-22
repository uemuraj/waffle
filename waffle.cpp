//
// Windows Update Agent API で Windows Update のチェック・ダウンロード・インストールをしてみる：
// 
// https://learn.microsoft.com/ja-jp/windows/win32/api/_wua/
// https://ascii.jp/elem/000/004/072/4072221/
// 

#include <locale>
#include <iostream>
#include "waffle.h"

int main()
{
    std::locale::global(std::locale(""));

    std::cout << "Hello World!\n";
}
