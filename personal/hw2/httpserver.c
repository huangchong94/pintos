#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

pthread_mutex_t thread_count_lock;
pthread_cond_t cond;
int num_threads_alive;
int threads_keep_alive;
pthread_t *threads;

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */

void  get_path(char *dest, char *request_path) {
  dest[0] = '\0';
  strcat(dest, server_files_directory);
  strcat(dest, request_path);
}

void serve_file(int fd, char *path, int file_size) {
  int srcfd = open(path, O_RDONLY, 0);
  http_serve_file(fd, srcfd, path, file_size);
  close(srcfd);
}

void serve_directory(int fd, char *path, int is_root) {
  char data[8192];
  data[0] = 0;
  char href[1024];
  DIR *dirp = opendir(path);
  struct dirent *entp;
  while ((entp=readdir(dirp))) {
    if (strcmp(entp->d_name, ".")==0 || strcmp(entp->d_name, "..")==0)
	  continue;
    sprintf(href, "<a href=\"%s\">%s</a> <br>", entp->d_name, entp->d_name);
    strcat(data, href);
    printf("%s\n", entp->d_name);
  }
  closedir(dirp);
  if (!is_root) 
    strcat(data, "<a href=\"../\">Parent directory</a>");
  http_serve_html(fd, data);
}

void handle_files_request(int fd) {
  /*
   * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
   * any existing code.
   */
  struct http_request *request = http_request_parse(fd);
  char path[4096];
  get_path(path, request->path);  
//  printf("request path: %s\n", path);
  if (access(path, F_OK)==-1) {
    printf("no %s\n", path);
    http_client_error(fd, 404, "...");
    return;
  }

  struct stat sbuf;
  stat(path, &sbuf);
  if (S_ISREG(sbuf.st_mode)) 
    serve_file(fd, path, sbuf.st_size);
  else if (S_ISDIR(sbuf.st_mode)) {
    char html_path[4096];
    strcpy(html_path, path);
    strcat(html_path, "/index.html");
    if ((access(html_path, F_OK)) != -1) 
      serve_file(fd, html_path, sbuf.st_size);
    else {
    printf("directory\n");
    int is_root = strcmp(request->path, "/")==0;
	serve_directory(fd, path, is_root);
    return;
    }
  }
  else {
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd,
      "<center>"
      "<h1>Welcome to httpserver!</h1>"
      "<hr>"
      "<p>Nothing's here yet.</p>"
      "</center>");
  }
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
}

void* thread_do(void (*request_handler)(int)) {
	pthread_mutex_lock(&thread_count_lock);	
	num_threads_alive++;
	if (num_threads_alive == num_threads) {
		pthread_cond_signal(&cond);
	}	
	pthread_mutex_unlock(&thread_count_lock);

	while (threads_keep_alive) {
		int client_fd = wq_pop(&work_queue);		
	//	printf("pop client fd %d\n", client_fd);
		if (client_fd!=-1 && threads_keep_alive) {
			void (*func) (int);
			func = (void (*) (int)) request_handler;
			func(client_fd);
			close(client_fd);
		}
	}

	pthread_mutex_lock(&thread_count_lock);
	num_threads_alive--;
	if (num_threads_alive == 0) {
		pthread_cond_signal(&cond);
	}
	pthread_mutex_unlock(&thread_count_lock);
	return NULL;
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
	threads_keep_alive = 1;
	num_threads_alive = 0;
	pthread_mutex_init(&thread_count_lock, NULL);
	pthread_cond_init(&cond, NULL);

	threads = (pthread_t*)malloc(sizeof(pthread_t)*num_threads);
	wq_init(&work_queue);	
	
	for (int i=0; i<num_threads; i++) {
		pthread_create(threads+i, NULL, (void*)&thread_do, request_handler);
		pthread_detach(*(threads+i));
	}
    
	pthread_mutex_lock(&thread_count_lock);
	while (num_threads_alive != num_threads) {
		pthread_cond_wait(&cond, &thread_count_lock);
	}
	pthread_mutex_unlock(&thread_count_lock);
}

void destroy_thread_pool() {
	threads_keep_alive = 0;
	wq_unblock_pop_requests(&work_queue);
	
	pthread_mutex_lock(&thread_count_lock);
	while (num_threads_alive != 0) {
		pthread_cond_wait(&cond, &thread_count_lock);
	}
	pthread_mutex_unlock(&thread_count_lock);
    
	free(threads);
}
/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
    
    wq_push(&work_queue, client_socket_number);	
//	printf("push client fd %d\n", client_socket_number);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
