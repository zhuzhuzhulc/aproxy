/*
* Name : Abhishek Bhowmick
* Andrew Id : abhowmi1
*
* Name : Rahul Muthoo
* Andrew Id : rmuthoo
*/

/*  NOTES :
*	1. Caching has been implemented
*	2. Proxy avoids termination in case of ECONNRESET and EPIPE through 
*      appropriate changes to rio_readnb and Rio_writen respectively
*   3. We ignore SIGPIPE signals as broken sockets can be detected later by Rio_readnb or Rio_writen 
*/


#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Maximum number of headers to be forwarded */
#define MAX_HEADERS 20


/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

/* $begin proxymain */
/*
 * proxy.c - A simple, iterative HTTP/1.0 Web Proxy server that has caching 
 */

void read_from_client(int client_connfd, int *nbr_headers,
					char headers[][MAXLINE], char *client_uri, 
					char *server_hostname, char *server_uri, int *server_port);
int read_requesthdrs(rio_t *rp, char headers[][MAXLINE]);
void parse_uri(char *uri, char *server_hostname, char *server_uri, 
				int *server_port); 
void request_server(int server_connfd, int nbr_headers, char headers[][MAXLINE],
					char *server_hostname, char *server_uri);
void write_http_line(int server_connfd, char *line);
void transfer_response_headers(rio_t *rp, int client_connfd, int *content_size);
int transfer_response_content(rio_t *rp, char *response, int client_connfd, 
								int bytes_left);
void get_filetype(char *filename, char *filetype);

// These functions are currently not in use
void serve_static(int fd, char *filename, int filesize);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);


int main(int argc, char **argv) 
{
    int listenfd, client_connfd, server_connfd, port, clientlen, server_port;
	int nbr_headers, content_size, bytes_read, bytes_left, currObjectSize, n;
	rio_t rio;
    struct sockaddr_in clientaddr;
	char client_uri[MAXLINE], server_hostname[MAXLINE], server_uri[MAXLINE];
	char headers[MAX_HEADERS][MAXLINE];	// Allow for a max of MAX_HEADERS
	char response[MAXBUF];
	char cacheObject[MAX_OBJECT_SIZE];
	char *currObject;

	cache_block* cacheData = NULL;
	char buf[MAXBUF], filetype[MAXLINE];

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }
    port = atoi(argv[1]);

	// Ignoring SIGPIPE signals
	Signal(SIGPIPE, SIG_IGN);
	initCache();

    listenfd = Open_listenfd(port);
    while (1) {
	clientlen = sizeof(clientaddr);
	client_connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
	read_from_client(client_connfd, &nbr_headers, headers, client_uri, 
					server_hostname, server_uri, &server_port);

	if((cacheData = SearchNode(client_uri)) != NULL)		// Cache hit
	{

		get_filetype(cacheData->url, filetype);
		// These headers are generated by the proxy
		sprintf(buf, "HTTP/1.0 200 OK\r\n");
    	sprintf(buf, "%sServer: Proxy Web Server\r\n", buf);
    	sprintf(buf, "%sContent-length: %d\r\n", buf, cacheData->length);
    	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    	Rio_writen(client_connfd, buf, strlen(buf));
		
		// Send the object to client
	   	Rio_writen(client_connfd, cacheData->data, cacheData->length);
			   	
	    Close(client_connfd);
	    continue;		// Move on to next transaction
		
	}

	server_connfd = Open_clientfd(server_hostname, server_port);
	request_server(server_connfd, nbr_headers, headers, server_hostname, 
					server_uri);
    Rio_readinitb(&rio, server_connfd);

	// Transfer response headers
	// get the content size of body in content_size
	content_size = 0;
	transfer_response_headers(&rio, client_connfd, &content_size);
   
	// Transfer response body
	bytes_read = 0;
	bytes_left = content_size;

	currObject = cacheObject;
	currObjectSize = 0;
	do{
		n = transfer_response_content(&rio,response,client_connfd,bytes_left);

		// copy the current object being served
		memcpy(currObject, response, n);
		currObject += n;
		currObjectSize += n;

		bytes_read += n;
		bytes_left -= n;
	}while(bytes_left > 0);

	// Ensure currObjectSize is same as content_size

	if(currObjectSize <= MAX_OBJECT_SIZE) // store data in cache
		StoreData(client_uri , cacheObject, currObjectSize);

	Close(client_connfd);
	Close(server_connfd);
    }
}
/* $end proxymain */


/*
 * read_from_client - reads the entire client HTTP request
 */
/* $begin read_from_client */
void read_from_client(int client_connfd, int *nbr_headers, char headers[][MAXLINE],
						 char *client_uri, char *server_hostname,
						 char *server_uri, int *server_port) 
{
    char buf[MAXLINE], method[MAXLINE], version[MAXLINE];
    rio_t rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, client_connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, client_uri, version);
	/* Check if the method is GET */
    if (strcasecmp(method, "GET")) { 
       clienterror(client_connfd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
        return;
    }

	/* Extract server hostname and uri from client uri */
	parse_uri(client_uri, server_hostname, server_uri, server_port);
	*nbr_headers = read_requesthdrs(&rio, headers);

}
/* $end read_from_client */


/*
 * read_requesthdrs - read and parse HTTP request headers
 * Returns nbr of headers read
 */
/* $begin read_requesthdrs */
int read_requesthdrs(rio_t *rp, char headers[][MAXLINE]) 
{
	char buf[MAXLINE];
	unsigned nbr_headers = 0;	

    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n") && nbr_headers < MAX_HEADERS) {
		// Ignore User-Agent, Accept, Accept--Encoding, Connection,
		// Proxy-Encoding headers
		if(!(strstr(buf, "User-Agent:")||strstr(buf, "Accept:")||
			strstr(buf, "Accept-Encoding:")||strstr(buf, "Connection:")||
			strstr(buf, "Proxy-Connection:")))
		{
			strcpy(headers[nbr_headers], buf);
			nbr_headers++;
		}
		Rio_readlineb(rp, buf, MAXLINE);
    }
    return nbr_headers;
}
/* $end read_requesthdrs */


/*
 *	transfer_response_headers - returns type of content data, 0 for binary and 1 for text
 */
void transfer_response_headers(rio_t *rp, int client_connfd, int *content_size)
{
	char buf[MAXLINE], tokens[MAXLINE];
	char *tok;

    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
		if(strstr(buf, "Content-length:")){	// Extract content length
			strcpy(tokens, buf);
			tok = strtok(tokens, ":");
			tok = strtok(NULL, ":");
			*content_size = atoi(tok);
		}

		Rio_writen(client_connfd, buf, strlen(buf));
    	Rio_readlineb(rp, buf, MAXLINE);
	}
	Rio_writen(client_connfd, buf, strlen(buf));

}

/*
 * parse_uri - parse URI into hostname and URI
 */
/* $begin parse_uri */

void parse_uri(char *client_uri, 
				char *server_hostname, char *server_uri, int *server_port) 
{
    char *ptr, *ret;
	char buf[MAXLINE], port[MAXLINE];
	size_t hostlen = 0, portlen = 0;

	ptr = index(client_uri, ':') + 3;
	strcpy(buf, ptr);

	if((ret = index(buf, ':')) == NULL){
		ret = index(buf, '/');	
		hostlen = strlen(ptr) - strlen(ret) ;
		*server_port = 80;				// Default HTTP port
	}
	else{
		hostlen = strlen(ptr) - strlen(ret);
		ptr = ret + 1;
		ret =  index(buf, '/');
		portlen = ret - ptr; 
		strncpy(port, buf + hostlen + 1, portlen);
		port[portlen] = '\0';
		*server_port = atoi(port);		// port specified by client
	}

	strncpy(server_hostname, buf, hostlen);	
	server_hostname[hostlen] = '\0';
	strcpy(server_uri, ret);
}
/* $end parse_uri */


/* 
 * request_server - send the request to server
 */
/* $begin request_server */
void request_server(int server_connfd, int nbr_headers, char headers[][MAXLINE],
					char *server_hostname, char *server_uri)
{
	char request[MAXLINE];	
	unsigned i = 0;
	sprintf(request, "GET %s HTTP/1.0\r\n", server_uri);
	write_http_line(server_connfd, request);

	if(!strstr(headers[i], "Host:")){
		sprintf(request, "Host: %s\r\n", server_hostname);
		write_http_line(server_connfd, request);
	}

	// Write compulsory headers
	write_http_line(server_connfd, (char *)user_agent_hdr);
	write_http_line(server_connfd, (char *)accept_hdr);
	write_http_line(server_connfd, (char *)accept_encoding_hdr);
	write_http_line(server_connfd, "Connection: close\r\n");
	write_http_line(server_connfd, "Proxy-Connection: close\r\n");

	// Write other headers supplied by client
	while(i < nbr_headers){
		write_http_line(server_connfd, headers[i]);
		i++;
	}

	//End request
	write_http_line(server_connfd, "\r\n");
}
/* $end request_server */

/* Write a line according to HTTP protocol */
void write_http_line(int server_connfd, char *line)
{
	Rio_writen(server_connfd, line, strlen(line));
}


/* 
 * read_from_server - Reads the response from server
 * Returns number of bytes read
 */
/* $begin read_from_server*/
int transfer_response_content(rio_t *rp, char *response, int client_connfd, int bytes_left)
{
	char buf[MAXLINE];
	int bytes_read = 0, n;
	int bytes_to_copy = 0;
	char *bufp = response;	
	

	if(MAXLINE <= bytes_left)	bytes_to_copy = MAXLINE;
	else						bytes_to_copy = bytes_left;

	// Handle premature proxy<->server socket connection end
	// Modified rio_readnb to prevent termination
	if((bytes_read = Rio_readnb(rp, buf, bytes_to_copy)) == 0){
    	clienterror(client_connfd, "GET failed", "ECONNRESET", "Very bad !",
    		"Connection to server closed prematurely");
	}

	memcpy(bufp, buf, bytes_read);

	Rio_writen(client_connfd, response, bytes_read);	

	return bytes_read;
}
/* $end read_from_server*/


// These implementations are currently not in use

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
    sprintf(body, "<html><title>Proxy Error</title>");
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
