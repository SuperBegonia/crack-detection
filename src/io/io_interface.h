#include <iostream>
#include <functional>
namespace fc_io
{

    class io_interface
    {
    public:
        //    io_interface(/* args */);
        virtual ~io_interface()
        {
        }
        virtual bool io_init() = 0;
        virtual void on_data_recev(std::function<void(const char *, int)> callback) = 0;
        virtual void stop_io() = 0;
        virtual void on_error(std::function<void(std::string err)>) = 0;
        virtual void write(const char *data, int len) = 0;
    };

}
