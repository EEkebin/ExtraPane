#pragma once
#include "input.hpp"
#include "media.hpp"
#include <dm/video_protocol.hpp>
#include <QThread>

namespace dm::linux_client {
class Receiver final:public QThread {
    Q_OBJECT
public:
    struct Options {
        QString server,local;
        dm::video::wire::ClientDisplay display;
        unsigned scale=0;
        bool input=true;
        unsigned interfaceIndex=0;
        unsigned port=43824;QString code;
    };
    Receiver(Options options,InputQueue& input,Media& media,QObject* parent=nullptr);
signals:
    void configure(unsigned width,unsigned height,unsigned fps,unsigned serial);
    void state(QString message);
    void statistics(QString text);
    void failed(QString reason);
protected:
    void run() override;
private:
    Options options_;
    InputQueue& input_;
    Media& media_;
};
}
