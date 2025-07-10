#include <nats.h>
#include "io_interface.h"

namespace fc_io
{

    void onMsg(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
    {
        // Prints the message, using the message getters:
        printf("Received msg: %s - %.*s\n",
               natsMsg_GetSubject(msg),
               natsMsg_GetDataLength(msg),
               natsMsg_GetData(msg));

        natsMsg_Destroy(msg);
    }

    void NatsTest()
    {
        natsConnection *nc = NULL;
        natsSubscription *sub = NULL;
        natsMsg *msg = NULL;
        auto status = natsConnection_ConnectTo(&nc, "nats://192.168.255.128:4222");
        if (status != NATS_OK)
        {
            printf("natsConnection_ConnectTo failed: %d\n", status);
            return;
        }
        printf("Connected to nats Ok");
        status = natsConnection_PublishString(nc, "foo", "hello world");

        // 订阅
        natsConnection_Subscribe(&sub, nc, ">", onMsg, NULL);

        if (status != NATS_OK)
        {
            printf("natsConnection_PublishString failed: %d\n", status);
            return;
        }
    }

    class nats_io : public io_interface
    {
    private:
        std::string user_;
        std::string pass_;
        std::function<void(const char *, int)> callback_ = nullptr;
        std::string read_topic_;
        const char *write_topic_;
        std::string url_;
        natsConnection *nc = NULL;
        natsSubscription *sub = NULL;
        natsStatus status;
        std::function<void(const std::string msg)> on_error_cb_ = nullptr;
        static void on_data(natsConnection *nc, natsSubscription *sub, natsMsg *msg, void *closure)
        {
            nats_io *obj = (nats_io *)closure;
            if (obj->callback_)
            {
                try
                {
                    obj->callback_(natsMsg_GetData(msg), natsMsg_GetDataLength(msg));
                }
                catch (const std::exception &e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
        }

    public:
        nats_io(std::string nats_url, std::string read_topic, std::string write_topic, std::string user = "", std::string pass = "")
        {

            this->url_ = nats_url;
            this->read_topic_ = read_topic;
            this->write_topic_ = write_topic.c_str();
            this->user_ = user;
            this->pass_ = pass;
        }
        ~nats_io()
        {
        }

        void on_data_recev(std::function<void(const char *, int)> callback) override
        {
            this->callback_ = callback;
        }
        bool io_init() override
        {
            natsStatus status;
            natsOptions *opts = NULL;

            // 创建 NATS 连接选项
            status = natsOptions_Create(&opts);
            if (status != NATS_OK)
            {
                printf("Error creating NATS options: %s\n", natsStatus_GetText(status));
                return false;
            }

            // 设置连接超时时间（以毫秒为单位）
            natsOptions_SetTimeout(opts, 5000); // 5 秒

            // 设置无限自动重连
            natsOptions_SetMaxReconnect(opts, -1);    // 无限重连
            natsOptions_SetReconnectWait(opts, 2000); // 每次重连等待 2 秒
            natsOptions_SetURL(opts, url_.c_str());
            if (pass_ == "" || user_ == "")
            {
                status = natsConnection_Connect(&nc, opts);
                if (status == natsStatus::NATS_OK)
                {
                    printf("Connected to NATS\n");
                    natsConnection_Subscribe(&sub, nc, read_topic_.c_str(), on_data, this);
                    natsOptions_Destroy(opts); // 连接成功后销毁选项
                    return true;
                }
                else
                {
                    printf("Error connecting to NATS: %s\n", natsStatus_GetText(status));
                    natsOptions_Destroy(opts); // 连接失败后销毁选项
                    return false;
                }
            }

            natsOptions_Destroy(opts); // 当没有尝试连接时销毁选项
            return false;
        }

        void on_error(std::function<void(std::string err)> cb) override
        {
            this->on_error_cb_ = cb;
        }

        void write(const char *data, int len) override
        {
            status = natsConnection_Publish(nc, "ai.streaming", data, len);
            if (status != natsStatus::NATS_OK && on_error_cb_)
            {
                on_error_cb_("write data error");
            }
        }
        void write_subj(const char *subj, const char *data, int len)
        {
            status = natsConnection_Publish(nc, subj, data, len);
            if (status != natsStatus::NATS_OK && on_error_cb_)
            {
                printf("write data error \n");
                on_error_cb_("write data error");
            }
        }
        void stop_io() override
        {
        }
    };

}
