#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

// comment in out in product environment
#define debug 1

#define MAX_LINE_LENGTH 128
#define CONFIG_NUM_EXPECT 4

static int thread_should_stop = 0;
static pthread_t tid;
static struct MHD_Daemon *d;
static char *config_file = "CONFIG";

static long file_max_byte, file_expire, worker_period_minute;
static char storage_dir[MAX_LINE_LENGTH];


int config_loader()
{
    FILE *file = fopen(config_file, "r");
    if (file == NULL)
    {
        perror("Error opening config file");
        return 1;
    }

    char line[MAX_LINE_LENGTH];

    size_t config_count = 0;

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "file_max_byte:%ld", &file_max_byte) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "file_expire:%ld", &file_expire) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "worker_period_minute:%ld", &worker_period_minute) == 1)
        {
            config_count++;
        }
        else if (sscanf(line, "storage_dir:%s", storage_dir) == 1)
        {
            config_count++;
        }
    }

    fclose(file);

    if (config_count != CONFIG_NUM_EXPECT)
    {
        perror("config insufficient\n");
        return 1;
    }
}

void *worker_thread(void *arg)
{
    struct timespec ts;

    
    while (!thread_should_stop)
    {

#ifdef debug
        printf("dispatching thread wake up\n");
        sleep(1);
        printf("dispatching thread sleep\n");
        sleep(10);
        goto while_end;
#endif

        sleep(worker_period_minute * 60);

        // Open the directory
        DIR *dir = opendir(storage_dir);
        if (dir == NULL)
        {
            perror("Error opening directory");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // Skip "." and ".." entries
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            // Construct the full path of the file
            char file_path[128];
            snprintf(file_path, sizeof(file_path), "%s/%s", storage_dir, entry->d_name);

            // Delete the file
            if (remove(file_path) == 0)
            {
                printf("Deleted file: %s\n", file_path);
            }
            else
            {
                perror("Error deleting file");
            }
        }

        // Close the directory
        closedir(dir);
    while_end:
    }

    return NULL;
}

// Signal handler function to handle SIGINT and SIGTERM
void signal_handler(int signum)
{   
    thread_should_stop = 1;
    pthread_cancel(tid);
    printf("dispathcing thread end\n");
    MHD_stop_daemon(d);
    printf("server stoped\n");
    printf("bye\n");
    exit(0);
}

static enum MHD_Result
can_compress(struct MHD_Connection *con)
{
    const char *ae;
    const char *de;

    ae = MHD_lookup_connection_value(con,
                                     MHD_HEADER_KIND,
                                     MHD_HTTP_HEADER_ACCEPT_ENCODING);
    if (NULL == ae)
        return MHD_NO;
    if (0 == strcmp(ae,
                    "*"))
        return MHD_YES;
    de = strstr(ae,
                "deflate");
    if (NULL == de)
        return MHD_NO;
    if (((de == ae) ||
         (de[-1] == ',') ||
         (de[-1] == ' ')) &&
        ((de[strlen("deflate")] == '\0') ||
         (de[strlen("deflate")] == ',') ||
         (de[strlen("deflate")] == ';')))
        return MHD_YES;
    return MHD_NO;
}

static enum MHD_Result
body_compress(void **buf,
              size_t *buf_size)
{
    Bytef *cbuf;
    uLongf cbuf_size;
    int ret;

    cbuf_size = compressBound(*buf_size);
    cbuf = malloc(cbuf_size);
    if (NULL == cbuf)
        return MHD_NO;
    ret = compress(cbuf,
                   &cbuf_size,
                   (const Bytef *)*buf,
                   *buf_size);
    if ((Z_OK != ret) ||
        (cbuf_size >= *buf_size))
    {
        /* compression failed */
        free(cbuf);
        return MHD_NO;
    }

    free(*buf);
    *buf = (void *)cbuf;
    *buf_size = (size_t)cbuf_size;
    return MHD_YES;
}

static enum MHD_Result
ahc_echo(void *cls,
         struct MHD_Connection *connection,
         const char *url,
         const char *method,
         const char *version,
         const char *upload_data, size_t *upload_data_size, void **ptr)
{
    struct MHD_Response *response;
    enum MHD_Result ret;
    enum MHD_Result comp;

    FILE *file;
    char *file_buf;
    long file_size;
    char file_path[64];

    (void)cls;              /* Unused. Silent compiler warning. */
    (void)version;          /* Unused. Silent compiler warning. */
    (void)upload_data;      /* Unused. Silent compiler warning. */
    (void)upload_data_size; /* Unused. Silent compiler warning. */

    if (0 != strcmp(method, "GET"))
        return MHD_NO; /* unexpected method */
    if (!*ptr)
    {
        *ptr = (void *)1;
        return MHD_YES;
    }
    *ptr = NULL;

    // fetch the request resources
    if (strcmp(url, "/") == 0 || strcmp(url, "/index.html") == 0)
    {
        strcpy(file_path, "./index.html");
    }
    else
    {
        if (strncmp(url, "/css", 4) == 0 || strncmp(url, "/js", 3) == 0 || strncmp(url, "/pages", 6) == 0)
        {
            snprintf(file_path, sizeof(file_path), ".%s", url);
        }
        else
        {
            printf("request invalid url: %s\n", file_path);
            strcpy(file_path, "./index.html");
        }
    }

    printf("request resources: %s url: %s\n", file_path, url);

    file = fopen(file_path, "rb");
    if (NULL == file)
    {
        printf("local file: %s cannot be find\n", file_path);
        return MHD_NO;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    rewind(file);

    // Allocate memory for file content
    file_buf = malloc(file_size);
    if (NULL == file_buf)
    {
        fclose(file);
        return MHD_NO;
    }

    if (fread(file_buf, 1, file_size, file) != file_size)
    {
        fclose(file);
        free(file_buf);
        return MHD_NO;
    }

    fclose(file); // Close the file

    /* try to compress the body */
    comp = MHD_NO;
    if (MHD_YES ==
        can_compress(connection))
        comp = body_compress((void **)&file_buf,
                             &file_size);

    response = MHD_create_response_from_buffer(file_size,
                                               file_buf,
                                               MHD_RESPMEM_MUST_FREE);
    if (NULL == response)
    {
        free(file_buf);
        return MHD_NO;
    }

    if (MHD_YES == comp)
    {
        if (MHD_NO ==
            MHD_add_response_header(response,
                                    MHD_HTTP_HEADER_CONTENT_ENCODING,
                                    "deflate"))
        {
            MHD_destroy_response(response);
            return MHD_NO;
        }
    }
    ret = MHD_queue_response(connection,
                             200,
                             response);
    MHD_destroy_response(response);
    return ret;
}

int main(int argc, char *const *argv)
{
    signal(SIGINT, signal_handler);  // Handle Ctrl+C (SIGINT)
    signal(SIGTERM, signal_handler); // Handle termination (SIGTERM)

    if (argc != 2)
    {
        fprintf(stderr, "%s PORT\n", argv[0]);
        return 1;
    }

    // if (config_loader() == 1)
    // {
    //     fprintf(stderr, "config file load error\n");
    //     return 1;
    // }

    // Create the worker thread
    if (pthread_create(&tid, NULL, worker_thread, NULL) != 0)
    {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    printf("server start at port: %s\n", argv[1]);
    d = MHD_start_daemon(MHD_USE_AUTO | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                         atoi(argv[1]), NULL, NULL,
                         &ahc_echo, NULL,
                         MHD_OPTION_END);
    if (NULL == d)
    {
        fprintf(stderr, "server start failed\n");
        return 1;
    }

    (void)getc(stdin);

    thread_should_stop = 1;

    pthread_join(tid, NULL);
    printf("dispathcing thread end\n");
    MHD_stop_daemon(d);
    printf("server stoped\n");
    printf("bye\n");
    return 0;
}
