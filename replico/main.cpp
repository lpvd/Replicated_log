#include "replico_server.h"


 int main(int argc, char* argv[])
 {
     // Check command line arguments.
     if (argc != 5)
     {
         std::cerr << "Usage: replico <mode> <rootaddress> <nodeaddress1;nodeaddress2> <threads>"
                   << std::endl
                   << "Usage: replico <mode> <nodeadress> <rootaddress> <threads>" << std::endl
                   << std::endl
                   << "Example for main:\n"
                   << "    replico root 0.0.0.0:8080 0.0.0.0:8081;0.0.0.0:8082 1" << std::endl
                   << "    replico node 0.0.0.0:8081 0.0.0.0:8080 1" << std::endl
                   << "    replico node 0.0.0.0:8082 0.0.0.0:8080 1" << std::endl;
         return EXIT_FAILURE;
     }

     RServer server;

     server.start(argc, argv);

     return EXIT_SUCCESS;
 }
