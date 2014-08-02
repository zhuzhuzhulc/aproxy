#include <stdio.h>
#include "csapp.h"

/* Maximum number of headers to be forwarded */
#define MAX_HEADERS 20

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */

void doit(int fd);
void do_proxy(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv) 
{
    int listenfd, client_connfd, port, clientlen, server_port, nbr_headers;
    struct sockaddr_in clientaddr;
	char server_hostname[MAXLINE], server_uri[MAXLINE];
	// Allow for a maximum of MAX_HEADERS headers
	char headers[MAX_HEADERS][MAXLINE];	

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	client_connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
	//doit(connfd);
	//Serve a proxy transaction
	read_from_client(client_connfd, &nbr_headers, headers, server_hostname, server_uri,
						 &server_port);
	server_connfd = Open_clientfd(server_hostname, server_port);
	request_server(server_connfd, nbr_headers, headers, server_uri);
	read_from_server(server_connfd, response);
	respond_to_client(client_connfd, response);

	Close(client_connfd);
	Close(server_connfd);
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) { 
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);

    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs);
    if (stat(filename, &sbuf) < 0) {
	clienterror(fd, filename, "404", "Not found",
		    "Tiny couldn't find this file");
	return;
    }

    if (is_static) { /* Serve static content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't read the file");
	    return;
	}
	serve_static(fd, filename, sbuf.st_size);
    }
    else { /* Serve dynamic content */
	if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
	    clienterror(fd, filename, "403", "Forbidden",
			"Tiny couldn't run the CGI program");
	    return;
	}
	serve_dynamic(fd, filename, cgiargs);
    }
}
/* $end doit */

/*
 * do_proxy - handle one HTTP request/response proxy transaction
 */
/* $begin read_from_client */
void read_from_client(int fd, int *nbr_headers, char **headers, char *server_hostname, char *server_uri, int *server_port) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
	/* Check if the method is GET */
    if (strcasecmp(method, "GET")) { 
       clienterror(fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
        return;
    }

	/* Extract server hostname and uri from client uri */
	parse_uri(uri, server_hostname, server_uri, server_port);
	*nbr_headers = read_requesthdrs(&rio, headers);

}
/* $end read_from_client */


/*
 * read_requesthdrs - read and parse HTTP request headers
 * Returns nbr of headers read
 */
/* $begin read_requesthdrs */
int read_requesthdrs(rio_t *rp, char **headers) 
{

	unsigned headers = 0;	

    while(strcmp(buf, "\r\n") && headers < MAX_HEADERS) {
		Rio_readlineb(rp, headers[i], MAXLINE);
		// Ignore User-Agent, Accept, Accept--Encoding, Connection,
		// Proxy-Encoding headers
		if(!(strstr(headers[i], "User-Agent:")||strstr(headers[i], "Accept:")||strstr(headers[i], "Accept-Encoding:")||strstr(headers[i], "Connection:")||strstr(headers[i], "Proxy-Connection:")))
			headers++;
    }
    return headers;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into hostname and URI
 */
/* $begin parse_uri */

void parse_uri(char *uri, char *server_hostname, char *server_uri, int *server_port) 
{
    char *ptr, *ret;
	char buf[MAXLINE], port[MAXLINE];
	size_t hostlen = 0, portlen = 0;
	
	ptr = index(uri, ':') + 3;
	strcpy(buf, ptr);

	if((ret = index(buf, ':')) == NULL){
		ret = index(buf, '/');	
		hostlen = ret - ptr;
		*server_port = 80;				// Default HTTP port
	}
	else{
		hostlen = ret - ptr;
		ptr = ret + 1;
		ret =  index(buf, '/');
		portlen = ret - ptr; 
		strncpy(port, buf, portlen);
		port[portlen] = '\0';
		*server_port = atoi(port);		// port specified by cient
	}

	strncpy(server_hostname, buf, hostlen);	
	server_hostname[hostlen] = '\0';
	strcpy(server_uri, ret);
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize) 
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
 
    /* Send response headers to client */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
	strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
	strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))
	strcpy(filetype, "image/jpeg");
    else
	strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* child */
	/* Real server would set all CGI vars here */
	setenv("QUERY_STRING", cgiargs, 1); 
	Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
	Execve(filename, emptylist, environ); /* Run CGI program */
    }
    Wait(NULL); /* Parent waits for and reaps child */
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
