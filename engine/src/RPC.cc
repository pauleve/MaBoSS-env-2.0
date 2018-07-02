/* 
   MaBoSS (Markov Boolean Stochastic Simulator)
   Copyright (C) 2011-2018 Institut Curie, 26 rue d'Ulm, Paris, France
   
   MaBoSS is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.
   
   MaBoSS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.
   
   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA 
*/

/*
   Module:
     RPC.cc
     
     Authors:
     Eric Viara <viara@sysra.com>
     
     Date:
     May 2018
*/

#include <iostream>

#include "RPC.h"

static int rpc_hostNameToAddr(const char *name, struct in_addr *address)
{
  struct hostent *hp;

  if (NULL == (hp = gethostbyname(name))) {
    return 0;
  }

  memcpy((char *)address, (char *)hp->h_addr, hp->h_length);
  return 1;
}

static void rpc_socket_reuse_addr(int s)
{
  int val;
  val = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof(val)) < 0) {
    perror("setsockopt reuseaddr");
  }
}

static void rpc_socket_nodelay(int socket)
{

  int flag = 1;
  if (setsockopt(socket,
		 IPPROTO_TCP,     /* set option at TCP level */
		 TCP_NODELAY,     /* name of option */
		 (char *) &flag,  /* the cast is historical  cruft */
		 sizeof(int)) < 0) {
    perror("setsockopt nodelay");
  }
}

static void rpc_checkAFUnixPort(const char *portname)
{
  int r = access(portname, O_RDONLY);
  if (r < 0) {
    return;
  }

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    return;
  }

  struct sockaddr_un sock_un_name;
  struct sockaddr *sock_addr;

  sock_un_name.sun_family = AF_UNIX;
  strcpy(sock_un_name.sun_path, portname);
  sock_addr = (struct sockaddr *)&sock_un_name;

  r = connect(sock_fd, sock_addr, sizeof(sock_un_name));
  if (r < 0) {
    unlink(portname);
  }
  close(sock_fd);
}

static int isnumber(const char* portname)
{
  char c;
  while ((c = *portname++) != 0) {
    if (!(c >= '0' && c <= '9')) {
      return 0;
    }
  }
  return 1;
}

#define BUFFER_SIZE 4196

char* rpc_readStringData(int fd)
{
  size_t data_len = 0;
  size_t data_alloc = 0;
  char* data = NULL;
  ssize_t size;
  char buffer[BUFFER_SIZE];

  while ((size = read(fd, buffer, sizeof(buffer))) > 0) {
    if (data_len + size >= data_alloc) {
      data_alloc = 2 * (data_len + size);
      data = (char*)realloc(data, data_alloc);
    }
    memcpy(&data[data_len], buffer, size);
    data_len += size;
    if (data[data_len-1] == 0) {
      break;
    }
  }
  
  return data;
}

ssize_t rpc_writeStringData(int fd, const char* data, size_t len)
{
  static char zero[1] = {0};

  ssize_t size = write(fd, data, len);
  if (size > 0) {
    size += write(fd, zero, sizeof(zero));
  }
  return size;
}


int rpc_Server::bind(const char** p_rpc_portname)
{
  const char* hostname = host.c_str();
  const char* portname = port.c_str();
  char hname[128];
  port_h = new rpc_PortHandle();

  port_h->portname = strdup(portname);
  port_h->domain = isnumber(portname) ? AF_INET : AF_UNIX;
  port_h->type = SOCK_STREAM;

  if (port_h->domain == AF_INET) {
    if (p_rpc_portname) {
      *p_rpc_portname = NULL;
    }
    if ((port_h->u.in.sockin_fd = socket(AF_INET, port_h->type, 0)) < 0) {
      std::string errmsg = std::string("eyedb fatal error: unable to create inet socket port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }

    rpc_socket_reuse_addr(port_h->u.in.sockin_fd);
    rpc_socket_nodelay(port_h->u.in.sockin_fd);
      
    port_h->u.in.sock_in_name.sin_family = AF_INET;
    port_h->u.in.sock_in_name.sin_port   = htons(atoi(portname));
      
    if (hostname == NULL) {
      if (gethostname(hname, sizeof(hname)-1) < 0) {
	perror("eyedb fatal error: gethostname failed");
	return rpc_Error;
      }
      hname[sizeof(hname)-1] = 0;
    }
    else {
      strcpy(hname, hostname);
    }

    if (!rpc_hostNameToAddr(hname, &port_h->u.in.sock_in_name.sin_addr)) {
      const std::string errmsg = std::string("unknown host [") + hname + "]";
      fprintf(stderr, "%s", errmsg.c_str());
      return rpc_Error;
    }
      
    if (::bind(port_h->u.in.sockin_fd, (struct sockaddr *)&port_h->u.in.sock_in_name, sizeof(port_h->u.in.sock_in_name)) < 0 ) {
      const std::string errmsg = std::string("eyedb fatal error: bind (naming the socket) failed port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }

    if (rpc_isSocketValid(port_h->u.in.sockin_fd) && (port_h->type == SOCK_STREAM && ::listen(port_h->u.in.sockin_fd, 2) < 0)) {
      std::string errmsg = std::string("eyedb fatal error: listen for inet socket port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }
  }

  if (port_h->domain == AF_UNIX) {
    rpc_checkAFUnixPort(portname);
    if (p_rpc_portname) {
      *p_rpc_portname = portname;
    }

    if ((port_h->u.un.sockun_fd = socket(AF_UNIX, port_h->type, 0)) < 0) {
      std::string errmsg = std::string("eyedb fatal error: unable to create unix socket port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }

    port_h->u.un.sock_un_name.sun_family = AF_UNIX;
    strcpy(port_h->u.un.sock_un_name.sun_path, portname);

    if (::bind(port_h->u.un.sockun_fd, (struct sockaddr *)&port_h->u.un.sock_un_name, sizeof(port_h->u.un.sock_un_name)) < 0 ) {
      std::string errmsg = std::string("eyedb fatal error: bind (naming the socket) failed port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }

    chmod(portname, 0777);
    if (rpc_isSocketValid(port_h->u.un.sockun_fd) && ::listen(port_h->u.un.sockun_fd, 2) < 0 ) {
      std::string errmsg = std::string("eyedb fatal error: listen for unix socket port [") + port_h->portname + "]";
      perror(errmsg.c_str());
      return rpc_Error;
    }
  }

  return rpc_Success;
}

int rpc_Server::listen()
{
  int n, fd, new_fd;
  fd_set fds_ready_to_read;
  fd_set fds_used;
  rpc_PortHandle *portdb[sizeof(fd_set)*8];

  memset(portdb, 0, sizeof(portdb));

  FD_ZERO(&fds_used);

  if (port_h->domain == AF_INET) {
    fd = port_h->u.in.sockin_fd;
  } else if (port_h->domain == AF_UNIX) {
    fd = port_h->u.un.sockun_fd;
  } else {
    fd = -1;
  }
  
  portdb[fd] = port_h;

  FD_SET(fd, &fds_used);

  for (;;) {
    fds_ready_to_read = fds_used;
    
    n = select(fd+1, &fds_ready_to_read, 0, 0, 0);

    if (n < 0) {
      if (errno == EINTR) {
	continue;
      }
      else {
	perror("error in select");
	return rpc_Error;
      }
    }

    if (FD_ISSET(fd, &fds_ready_to_read)) {
      rpc_PortHandle *port_h;
      if ((port_h = portdb[fd]) != NULL) {
	/* we have a new connection */
	struct sockaddr *sock_addr;
	socklen_t length;

	if (port_h->domain == AF_INET) {
	  sock_addr = (struct sockaddr *)&port_h->u.in.sock_in_name;
	  length = sizeof(port_h->u.in.sock_in_name);
	} else {
	  sock_addr = (struct sockaddr *)&port_h->u.un.sock_un_name;
	  length = sizeof(port_h->u.un.sock_un_name);
	}

	if ((new_fd = accept(fd, sock_addr, &length)) < 0) {
	  perror("accept connection");
	  return rpc_Error;
	}

	if (new_fd >= 0) {
	  if (fork() == 0) {
	    char* request = rpc_readStringData(new_fd);
	    if (request != NULL) {
	      manageRequest(new_fd, request);
	      free(request);
	    }
	    close(new_fd);
	    exit(0);
	  }
	  close(new_fd);
	}
      }
    }
  }

  return rpc_Success;
}

int rpc_Client::open()
{
  const char* hostname = host.c_str();
  const char* portname = port.c_str();
  int domain;
  int length;
  struct sockaddr_in sock_in_name;
  struct sockaddr_un sock_un_name;
  struct sockaddr *sock_addr;

  std::string errmsg = "";

  int type = SOCK_STREAM;

  if (isnumber(portname)) {
    char hname[64];
    domain = AF_INET;
    sock_in_name.sin_family = domain;
    sock_in_name.sin_port = htons(atoi(portname));

    strcpy(hname, hostname);

    if (!rpc_hostNameToAddr(hname, &sock_in_name.sin_addr)) {
      errmsg = std::string("unknown host: " ) + hostname;
      perror(errmsg.c_str());
      return rpc_Error;
    }
    sock_addr = (struct sockaddr *)&sock_in_name;
    length = sizeof(sock_in_name);
  } else {
    domain = AF_UNIX;
    if (hostname) {
      if (!rpc_hostNameToAddr(hostname, &sock_in_name.sin_addr)) {
	errmsg = std::string("unknown host: " ) + hostname;
	perror(errmsg.c_str());
	return rpc_Error;
      }

      if (strcmp(hostname, "localhost")) {
	errmsg = std::string("localhost expected (got ") + hostname + ") for named pipe " + portname;
	perror(errmsg.c_str());
	return rpc_Error;
      }	
    }

    sock_un_name.sun_family = domain;
    strcpy(sock_un_name.sun_path, portname);
    sock_addr = (struct sockaddr *)&sock_un_name;
    length = sizeof(sock_un_name);
  }

  if ((sock_fd = socket(domain, type, 0))  < 0) {
    errmsg = std::string("server unreachable: ") + "host " + hostname + ", port " + portname;
    perror(errmsg.c_str());
    return rpc_Error;
  }

  if (connect(sock_fd, sock_addr, length) < 0) {
    errmsg = std::string("server unreachable: ") + "host " + hostname + ", port " + portname;
    perror(errmsg.c_str());
    return rpc_Error;
  }

  return rpc_Success;
}

int rpc_Client::close()
{
  return ::close(sock_fd);
}