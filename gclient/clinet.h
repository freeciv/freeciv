#ifndef __CLINET_H
#define __CLINET_H
#define DEFAULT_SOCK_PORT 5555

struct connection;





int connect_to_server(char *name, char *hostname, int port, char *errbuf);
void close_server_connection(void);
/*
void get_net_input(XtPointer client_data, int *fid, XtInputId *id);
int client_open_connection(char *host, int port);
void connection_gethostbyaddr(struct connection *pconn, char *desthost);
*/




extern struct connection aconnection;
#endif
