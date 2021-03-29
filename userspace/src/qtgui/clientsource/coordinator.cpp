// SPDX-License-Identifier: GPL-2.0-or-later
#include "coordinator.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMapIterator>
#include <QJsonObject>
#include <QNetworkDatagram>

extern "C" {
    #include "names.h"
    #include "usbip_common.h"
    #include "../kcommon.h"
}

Coordinator::Coordinator(WebBridge *bridge, QObject *parent) : QObject(parent), bridge(bridge)
{
    connect(bridge, &WebBridge::toApp, this, &Coordinator::processWeb);
}

// I decided to go with void because I attempted callback
//  through channel and it was always null in js world.
void Coordinator::processWeb(const QVariantMap &input)
{
    auto process = input["process"].toString();

    if (0 == process.compare("detach")) {
        bool ok;
        auto port(input["port"].toInt(&ok));
        if (ok) {
            usbipc_detach(port);
        }
        return;
    }

    if (0 == process.compare("attach")) {
        auto host(input["host"].toString().toStdString());
        auto busid(input["busid"].toString().toStdString());

        if (host.compare("") > 0 && busid.compare("") > 0) {
            usbipc_attach((char*)host.c_str(), (char*)busid.c_str());
        }
        return;
    }

    if (0 == process.compare("port")) {
        QVariantList devices;
        usbip_devices* linked_device = usbip_list_imported();
        usbip_devices* current_device = nullptr;
        while(linked_device != nullptr) {
            devices.push_back(QVariantMap({
                {"port", linked_device->port},
                {"product_name", linked_device->product_name}
            }));
            current_device = linked_device;
            linked_device = linked_device->next;
            usbip_devices_free(current_device);
        }
        bridge->toWeb(QVariantMap({{"devices", devices}}));
        return;
    }

    if (0 == process.compare("list")) {
        QVariantList devices;
        auto host(input["host"].toString().toStdString());
        usbip_external_list* linked_device = usbip_list_remote((char*)host.c_str());
        usbip_external_list* current_device = nullptr;
        while(linked_device != nullptr) {
            QVariantList interfaces;
            for(int i = 0; i < linked_device->num_interfaces; i++) {
                interfaces.push_back(linked_device->interfaces[i]);
            }
            devices.push_back(QVariantMap({
                {"product_name", linked_device->product_name},
                {"busid", linked_device->busid},
                {"path", linked_device->path},
                {"interfaces", interfaces}
            }));
            current_device = linked_device;
            linked_device = linked_device->next;
            usbip_external_list_free(current_device);
        }
        bridge->toWeb(QVariantMap({
            {"devices", devices},
            {"host", input["host"]}
        }));
    }
    if (0 == process.compare("settings")) {
        auto save = input["save"].toBool();
        if (save) {
            auto vals(input["settings"].toMap());
            for (auto i = vals.constBegin(); i != vals.constEnd(); ++i) {
                settings.setValue(i.key(), i.value());
            }
        } else {
            QVariantMap output;
            for(auto key: settings.childKeys()) {
                output.insert(key, settings.value(key));
            }
            bridge->toWeb({
                {"process", "settings"},
                {"settings", output}
            });
       }
    }
    if (0 == process.compare("find")) {
       getNotifier()->find();
    }
    if (0 == process.compare("listout")) {
       auto host(input["host"].toString());
       getNotifier()->listAdmin(host);
    }
    if (0 == process.compare("bind")) {
        auto host(input["host"].toString());
        auto busid(input["busid"].toString());
        getNotifier()->bind(host, busid);
    }
}

void Coordinator::sendHost(const QNetworkDatagram datagram)
{
  bridge->toWeb({
    {"process", "hostFound"},
    {"data", datagram.data()},
    {"host", datagram.senderAddress().toString()}
  });
}

void Coordinator::sendDgram(const QNetworkDatagram datagram) {

  usbip_names_init();
  auto dataDevices = QJsonDocument::fromJson(datagram.data());
  QJsonArray arr;
  if(dataDevices.isArray()) {
    for (auto device : dataDevices.array()) {
        QJsonObject obj(device.toObject());
        auto prodInt(obj["product"].toString().toUInt(nullptr,16));
        auto vendInt(obj["vendor"].toString().toUInt(nullptr,16));
        const char* vendor = names_vendor(vendInt);
        const char* product = names_product(vendInt, prodInt);
        arr.append(QJsonObject({
           {"product", obj["product"].toString()},
           {"vendor", obj["vendor"].toString()},
           {"vname", vendor },
           {"pname", product },
           {"busid", obj["busid"].toString()}
        }));
    }
    bridge->toWeb({
      {"process", "datagram"},
      {"data", arr},
      {"host", datagram.senderAddress().toString()}
    });
    return;
  }

  bridge->toWeb({
    {"process", "datagram"},
    {"data", dataDevices},
    {"host", datagram.senderAddress().toString()}
  });

  usbip_names_free();
}



GroupNotifier *Coordinator::getNotifier()
{
  if (notifier == nullptr) {
      const qint16 hostPort = settings.value("multicast/hostPort", 5191).toInt(); // AIM aint usin it
      QString groupIPV4Addr = settings.value("multicast/ipv4addr", "239.255.22.71").toString();
      notifier = new GroupNotifier(groupIPV4Addr, hostPort);
      connect(notifier, &GroupNotifier::hostFound, this, &Coordinator::sendHost);
      connect(notifier, &GroupNotifier::dgramArrived, this, &Coordinator::sendDgram);
   }

  return notifier;
}
