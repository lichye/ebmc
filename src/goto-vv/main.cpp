#include <iostream>
#include <string_view>
int compile_design(int, char **);
int constrain_design(int, char **);
int main(int argc, char **argv)
{
  if(argc > 1)
  {
    const std::string_view command(argv[1]);
    if(command == "compile")
      return compile_design(argc - 1, argv + 1);
    if(command == "constrain")
      return constrain_design(argc - 1, argv + 1);
  }
  std::cerr << "usage: goto-vv compile OUTPUT [slang options] INPUT\n"
               "       goto-vv constrain INPUT CONTRACT OUTPUT\n";
  return 2;
}
