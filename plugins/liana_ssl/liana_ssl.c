/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Daemon
 *  ------------------
 *  Copyright (C) 2010, Jonathan Gonzalez V. <zeus@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE


#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "config.h"
#include "plugin.h"
#include "MKPlugin.h"

#include <matrixssl/matrixsslApi.h>

/* Plugin data for register */
MONKEY_PLUGIN("liana_ssl", "Liana SSL Network", "0.1", MK_PLUGIN_CORE_PRCTX | MK_PLUGIN_NETWORK_IO);

struct plugin_api *mk_api;

struct mk_liana_ssl
{
    ssl_t *ssl;
    int socket_fd;
    struct mk_list cons;
};

sslKeys_t *keys;
char *cert_file;
char *key_file;

pthread_key_t _mkp_data;

int liana_conf(char *confdir) {
    int ret = 0;
    unsigned long len;
    char *conf_path;
    struct mk_config_section *section;
    struct mk_config *conf;

    /* Read palm configuration file */
    mk_api->str_build(&conf_path, &len, "%s/liana_ssl.conf", confdir);
    conf = mk_api->config_create(conf_path);
    section = conf->section;

    while (section) {
        /* Just read PALM sections... yes it's a joke for edsiper XD */
        if (strcasecmp(section->name, "LIANA_SSL") != 0) {
            section = section->next;
            continue;
        }

        cert_file = mk_api->config_section_getval(section, "CertFile", MK_CONFIG_VAL_STR);
#ifdef TRACE
        PLUGIN_TRACE("Register Certificate File '%s'", cert_file);
#endif

        key_file = mk_api->config_section_getval(section, "KeyFile", MK_CONFIG_VAL_STR);
#ifdef TRACE
        PLUGIN_TRACE("Register Key File '%s'", key_file);
#endif

        section = section->next;
    }

    mk_api->mem_free(conf_path);

    return ret;
}

int liana_ssl_handshake(struct mk_liana_ssl *conn) {
    unsigned char *buf = NULL;
    unsigned char *buf_sent = NULL;
    int len;
    int ret = 0;
    ssize_t bytes_read;
    ssize_t bytes_sent;

#ifdef TRACE
    PLUGIN_TRACE( "Trying to handshake" );
#endif

    while( ret != MATRIXSSL_HANDSHAKE_COMPLETE ) {
        len = matrixSslGetReadbuf( conn->ssl, &buf );

        if( len == PS_ARG_FAIL ) {
#ifdef TRACE
            PLUGIN_TRACE( "Error trying to read data for handshake" );
#endif
            return -1;
        }

        bytes_read = read( conn->socket_fd, (void *)buf, len );

        if( bytes_read < 0 ) {
#ifdef TRACE
            PLUGIN_TRACE( "Error reading data from buffer" );
#endif
            return -1;
        }

#ifdef TRACE
        PLUGIN_TRACE( "Read %d data for handshake", bytes_read );
#endif

        ret = matrixSslReceivedData( conn->ssl, bytes_read, (unsigned char **)&buf, (uint32 *)&len);

        if( ret == MATRIXSSL_REQUEST_RECV ) continue;

        if( ret == PS_MEM_FAIL  || ret == PS_ARG_FAIL || ret == PS_PROTOCOL_FAIL ) {
#ifdef TRACE
            PLUGIN_TRACE( "An error occurred while trying to decode the ssl data" );
#endif
            return -1;
        }

        if( ret == MATRIXSSL_HANDSHAKE_COMPLETE ) {
#ifdef TRACE
            PLUGIN_TRACE( "Ssl handshake complete!" );
#endif
            return 0;
        }

        if( ret == MATRIXSSL_REQUEST_SEND ) {
#ifdef TRACE
            PLUGIN_TRACE( "The handshake needs to send data" );
#endif
            do {
                len = matrixSslGetOutdata( conn->ssl, &buf_sent );

                if( len == 0 ) break;

                if( len == PS_ARG_FAIL ) {
#ifdef TRACE
                    PLUGIN_TRACE( "Error trying to send data during the handshake" );
#endif
                    return -1;
                }

                bytes_sent = write( conn->socket_fd, (void *)buf_sent, len);
                if( bytes_sent == -1 ) {
#ifdef TRACE
                    PLUGIN_TRACE( "An error ocurred trying to send data" );
#endif
                    return -1;
                }
#ifdef TRACE
                PLUGIN_TRACE( "Has sent %d of %d data to end the handshake ", bytes_sent, len );
#endif
                ret = matrixSslSentData( conn->ssl, (uint32)bytes_sent );

                if( ret == MATRIXSSL_REQUEST_CLOSE ) {
#ifdef TRACE
                    PLUGIN_TRACE( "Success we should close the session, why?" );
#endif
                    return -1;
                }

                if( ret == PS_ARG_FAIL ) {
#ifdef TRACE
                    PLUGIN_TRACE( "Error sending data during handshake" );
#endif
                    return -1;
                }

            } while( ret != MATRIXSSL_SUCCESS || ret != MATRIXSSL_HANDSHAKE_COMPLETE );
#ifdef TRACE
            PLUGIN_TRACE( "Handshake complete!" );
#endif
        }
    }

    return 0;
}

int _mkp_init(void **api, char *confdir)
{
    mk_api = *api;

    liana_conf(confdir);

    return 0;
}

void _mkp_exit()
{
}

int _mkp_network_io_accept(int server_fd, struct sockaddr_in sock_addr)
{
    int remote_fd;
    socklen_t socket_size = sizeof(struct sockaddr_in);

#ifdef TRACE
    PLUGIN_TRACE("Accepting Connection");
#endif

    /* remote_fd = accept4(server_fd, (struct sockaddr *) &sock_addr, */
    /*                     &socket_size, SOCK_NONBLOCK); */

    remote_fd = accept(server_fd, (struct sockaddr *) &sock_addr, &socket_size);

    if( remote_fd == -1 ) {
#ifdef TRACE
        PLUGIN_TRACE( "Error accepting connection" );
#endif
        return -1;
    }

    return remote_fd;
}

int _mkp_network_io_read(int socket_fd, void *buf, int count)
{
    ssize_t bytes_read;
    struct mk_list *list_head = (struct mk_list *)pthread_getspecific(_mkp_data);
    struct mk_list *curr;
    struct mk_liana_ssl *conn = NULL;
    int ret;
    int len;
    unsigned char *ssl_buf = NULL;

#ifdef TRACE
    PLUGIN_TRACE( "Locating socket on ssl connections list" );
#endif
    mk_list_foreach(curr, list_head) {
        conn = mk_list_entry( curr, struct mk_liana_ssl, cons);
        if( conn->socket_fd == socket_fd )
            break;
        conn = NULL;
    }
    if( conn == NULL ) return -1;

#ifdef TRACE
    PLUGIN_TRACE("Reading");
#endif

    do {
        len = matrixSslGetReadbuf( conn->ssl, &ssl_buf );

        bytes_read = read(socket_fd, (void *)ssl_buf, len);

#ifdef TRACE
        PLUGIN_TRACE( "Decoding data from ssl connection" );
#endif

        ret = matrixSslReceivedData( conn->ssl, bytes_read, (unsigned char **)&ssl_buf, (uint32 *)&len);

        if( ret == PS_MEM_FAIL  || ret == PS_ARG_FAIL || ret == PS_PROTOCOL_FAIL ) {
#ifdef TRACE
            PLUGIN_TRACE( "An error occurred while trying to decode the ssl data" );
#endif
            return -1;
        }
    } while ( ret == MATRIXSSL_REQUEST_RECV  && ret != MATRIXSSL_APP_DATA);

    if( ret == MATRIXSSL_RECEIVED_ALERT ) {
        if( *ssl_buf == SSL_ALERT_LEVEL_FATAL ) {
#ifdef TRACE
            PLUGIN_TRACE( "A fatal alert has raise, we must close the connection" );
#endif
            return 0;
        } else {
            return 0;
        }
    }


    strncpy( (char *)buf, (const char *)ssl_buf, count );
    bytes_read = len;

    return bytes_read;
}

int _mkp_network_io_write(int socket_fd, const void *buf, size_t count )
{
    ssize_t bytes_sent = -1;
    struct mk_list *list_head = (struct mk_list *)pthread_getspecific(_mkp_data);
    struct mk_list *curr;
    struct mk_liana_ssl *conn = NULL;
    char *buf_sent;
    char *buf_sent_to;
    int ret;
    int len;

#ifdef TRACE
    PLUGIN_TRACE("Write");
#endif

    mk_list_foreach(curr, list_head) {
        conn = mk_list_entry( curr, struct mk_liana_ssl, cons);
        if( conn->socket_fd == socket_fd )
            break;
        conn = NULL;
    }
    if( conn == NULL ) return -1;

    len = matrixSslGetWritebuf( conn->ssl, (unsigned char **)&buf_sent, count);
    strncpy( buf_sent, buf, count);
    len = matrixSslEncodeWritebuf( conn->ssl, count );

    len = matrixSslGetOutdata( conn->ssl, (unsigned char **)&buf_sent_to );

    if( len < 0 ) {
#ifdef TRACE
        PLUGIN_TRACE( "Error encoding data to send" );
#endif
        return 0;
    }

    bytes_sent = write(socket_fd, buf_sent_to, len);

    ret = matrixSslSentData( conn->ssl, bytes_sent );

    return bytes_sent;
}

int _mkp_network_io_writev(int socket_fd, struct mk_iov *mk_io)
{
    ssize_t bytes_sent = -1;
    int i;
#ifdef TRACE
    PLUGIN_TRACE("WriteV");
#endif

    for( i = 0; i < mk_io->iov_idx; i++ ) {
        bytes_sent = _mkp_network_io_write(socket_fd, mk_io->io[i].iov_base, mk_io->io[i].iov_len);
    }

    return bytes_sent;
}

int _mkp_network_io_close(int socket_fd)
{
    close(socket_fd);
    return 0;
}

int _mkp_network_io_connect(int socket_fd, char *host, int port)
{
    int res;
    struct sockaddr_in *remote;

    remote = (struct sockaddr_in *)
        mk_api->mem_alloc_z(sizeof(struct sockaddr_in));
    remote->sin_family = AF_INET;

    res = inet_pton(AF_INET, host, (void *) (&(remote->sin_addr.s_addr)));

    if (res < 0) {
        perror("Can't set remote->sin_addr.s_addr");
        mk_api->mem_free(remote);
        return -1;
    }
    else if (res == 0) {
        perror("Invalid IP address\n");
        mk_api->mem_free(remote);
        return -1;
    }

    remote->sin_port = htons(port);
    if (connect(socket_fd,
                (struct sockaddr *) remote, sizeof(struct sockaddr)) == -1) {
        close(socket_fd);
        perror("connect");
        return -1;
    }

    mk_api->mem_free(remote);

    return 0;
}

int _mkp_network_io_send_file(int socket_fd, int file_fd, off_t *file_offset,
                              size_t file_count)
{
    ssize_t bytes_written = -1;
    void *buf_file = mk_api->mem_alloc( file_count );
    ssize_t len;

#ifdef TRACE
    PLUGIN_TRACE( "Send file");
#endif


    len = pread(file_fd, buf_file, file_count, *file_offset);
    if( len == -1 ) {
        perror( "error leyendo? :S");
        return -1;
    }
    bytes_written = _mkp_network_io_write(socket_fd, buf_file, file_count);
    if (bytes_written == -1) {
        perror( "error from sendfile" );
        return -1;
    }

    return bytes_written;
}

int _mkp_network_io_create_socket(int domain, int type, int protocol)
{
    int socket_fd;
#ifdef TRACE
    PLUGIN_TRACE("Create Socket");
#endif
    socket_fd = socket(domain, type, protocol);

    return socket_fd;
}

int _mkp_network_io_bind(int socket_fd, const struct sockaddr *addr, socklen_t addrlen, int backlog)
{
    int ret;

    ret = bind(socket_fd, addr, addrlen);

    if( ret == -1 ) {
        perror("Error binding socket");
        return ret;
    }

    ret = listen(socket_fd, backlog);

    if(ret == -1 ) {
        perror("Error setting up the listener");
        return -1;
    }

    return ret;
}

int _mkp_network_io_server(int port, char *listen_addr)
{
    int socket_fd;
    int ret;
    struct sockaddr_in local_sockaddr_in;

#ifdef TRACE
    PLUGIN_TRACE("Create SSL socket");
#endif

    socket_fd = _mkp_network_io_create_socket(PF_INET, SOCK_STREAM, 0);
    if( socket_fd == -1) {
        perror("Error creating server socket");
#ifdef TRACE
        PLUGIN_TRACE("Error creating server socket");
#endif
        return -1;
    }
    mk_api->socket_set_tcp_nodelay(socket_fd);

    local_sockaddr_in.sin_family = AF_INET;
    local_sockaddr_in.sin_port = htons(port);
    inet_pton(AF_INET, listen_addr, &local_sockaddr_in.sin_addr.s_addr);
    memset(&(local_sockaddr_in.sin_zero), '\0', 8);

    mk_api->socket_reset(socket_fd);

    ret = _mkp_network_io_bind(socket_fd, (struct sockaddr *) &local_sockaddr_in,
                               sizeof(struct sockaddr), mk_api->sys_get_somaxconn());

    if(ret == -1) {
#ifdef TRACE
        PLUGIN_TRACE("Error: Port %i cannot be used", port);
#endif
        return -1;
    }

#ifdef TRACE
    PLUGIN_TRACE("Socket created, returned socket");
#endif


    return socket_fd;
}

int _mkp_core_prctx(struct server_config *config)
{
    if( matrixSslOpen() < 0 ) {
#ifdef TRACE
        PLUGIN_TRACE("Can't start matrixSsl");
#endif
        return 0;
    }

#ifdef TRACE
    PLUGIN_TRACE("MatrixSsl Started");
#endif

    if( matrixSslNewKeys( &keys ) < 0 ) {
#ifdef TRACE
        PLUGIN_TRACE( "MatrixSSL couldn't init the keys" );
#endif
        return 0;
    }

    if( matrixSslLoadRsaKeys( keys, cert_file, key_file, NULL, NULL ) < 0 ) {
#ifdef TRACE
        PLUGIN_TRACE( "MatrixSsl couldn't read the certificates" );
#endif
        return 0;
    }

#ifdef TRACE
    PLUGIN_TRACE( "MatrixSsl just read the certificates, ready to go!" );
#endif



    return 0;
}

void _mkp_core_thctx() {
    struct mk_list *list_head = mk_api->mem_alloc(sizeof(struct mk_list));

    mk_list_init(list_head);
    pthread_setspecific(_mkp_data, list_head);
}

int _mkp_event_read(int socket_fd) {
    int ret;
    struct mk_list *list_head = (struct mk_list *)pthread_getspecific(_mkp_data);
    struct mk_list *curr;
    struct mk_liana_ssl *conn;

    mk_list_foreach(curr, list_head) {
        conn = mk_list_entry( curr, struct mk_liana_ssl, cons );
        if(conn->socket_fd == socket_fd)
            return MK_PLUGIN_RET_EVENT_NOT_ME;
    }

    conn = (struct mk_liana_ssl *) malloc(sizeof(struct mk_liana_ssl));

    if((ret = matrixSslNewServerSession( &conn->ssl, keys, NULL )) < 0) {
#ifdef TRACE
        PLUGIN_TRACE( "Error initiating the ssl session" );
#endif
        matrixSslDeleteSession( conn->ssl );
        return -1;
    }
#ifdef TRACE
    PLUGIN_TRACE( "Ssl session started" );
#endif

    conn->socket_fd = socket_fd;

    mk_list_add( &conn->cons, list_head );

    ret = liana_ssl_handshake( conn );

    if( ret != 0 ) {
#ifdef TRACE
        PLUGIN_TRACE( "Error trying to handshake with the client" );
#endif
        return -1;
    }



    return 0;
}
