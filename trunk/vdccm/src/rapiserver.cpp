//
// C++ Implementation: rapiserver
//
// Description:
//
//
// Author: Volker Christian <voc@users.sourceforge.net>, (C) 2006
//
// Copyright: See COPYING file that comes with this distribution
//
//
#include "rapiserver.h"
#include "rapiconnection.h"
#include "rapihandshakeclientfactory.h"
#include "rapiprovisioningclientfactory.h"
#include "rapihandshakeclient.h"
#include "rapiprovisioningclient.h"
#include "rapiproxyfactory.h"
#include <synce_log.h>
#include <synce.h>
#include <errno.h>

#include <arpa/inet.h>

using namespace std;

RapiServer::RapiServer(RapiHandshakeClientFactory *rhcf, RapiProvisioningClientFactory *rpcf, u_int16_t port, string interfaceName)
    : TCPServerSocket(NULL, port, interfaceName)
{
    rapiHandshakeClientFactory = rhcf;
    rapiProvisioningClientFactory = rpcf;
}


RapiServer::~RapiServer()
{
}


void RapiServer::disconnect(string deviceIpAddress)
{
    map<string, RapiConnection*>::iterator it = rapiConnection.find(deviceIpAddress);
    RapiConnection *rc = (*it).second;
    rapiConnection.erase(it);
    delete rc;
}


#include <iostream>
void RapiServer::event()
{
    std::cout << "990-server-event()" << endl;

    int fd;

    fd = ::accept(getDescriptor(), NULL, NULL);

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    if (fd < 0) {
        synce_warning(strerror(errno));
        return;
    }

    if (getpeername(fd, (struct sockaddr *) &addr, &len) < 0) {
        synce_warning(strerror(errno));
        return;
    }

    char remoteIpAddress[16];
    if (!inet_ntop(AF_INET, &addr.sin_addr, remoteIpAddress, sizeof(remoteIpAddress))) {
        remoteIpAddress[0] = '\0';
    }

    if (rapiConnection[remoteIpAddress] == NULL) {
        char *path;

        if (!synce::synce_get_directory(&path)) {
            return;
        }
        string socketPath = string(path) + "/" + remoteIpAddress;
        free(path);
        std::cout << "RapiHandshakeClient for device " << remoteIpAddress << std::endl;
        // Rapi Handshake Client
        rapiConnection[remoteIpAddress] = new RapiConnection(new RapiProxyFactory(), socketPath, this, remoteIpAddress);
        rapiConnection[remoteIpAddress]->setHandshakeClient(dynamic_cast<RapiHandshakeClient *>(rapiHandshakeClientFactory->socket(fd, this)));
    } else {
        if (rapiConnection[remoteIpAddress]->getRapiProvisioningClient() == NULL) {
            std::cout << "RapiProvisioningClient for device " << remoteIpAddress << std::endl;
            // Rapi Provisioning Client
            rapiConnection[remoteIpAddress]->setProvisioningClient(dynamic_cast<RapiProvisioningClient *>(rapiProvisioningClientFactory->socket(fd, this)));
            rapiConnection[remoteIpAddress]->keepAlive();
        } else {
            // We only accept two connections to port 990 from one particular ip-address
            ::shutdown(fd, 2);
            ::close(fd);
        }
    }
}
