#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;

    #define PRINT_SOCK_ERROR(msg) fprintf(stderr, "%s: %d\n", msg, WSAGetLastError())
    #define CLOSE_SOCKET(s) closesocket(s)
#elif __unix__
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    typedef int socket_t;
    
    #define INVALID_SOCKET (-1)
    #define PRINT_SOCK_ERROR(msg) perror(msg)
    #define CLOSE_SOCKET(s) close(s)
    #define SOCKET_ERROR  (-1)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <stdio.h>
#include <errno.h>
#include "miniaudio.h"

#define PORT "6969"

#define FORMAT ma_format_f32
#define CHANNELS 2
#define SAMPLE_RATE 48000
#define FRAMES_PER_PACKET 256

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    void *pBuffer;
    ma_uint32 framesToWrite = frameCount;

    if (ma_pcm_rb_acquire_write((ma_pcm_rb *)pDevice->pUserData, &framesToWrite, &pBuffer) == MA_SUCCESS)
    {
        ma_copy_pcm_frames(pBuffer, pInput, framesToWrite, pDevice->capture.format, pDevice->capture.channels); 
        ma_pcm_rb_commit_write((ma_pcm_rb *)pDevice->pUserData, framesToWrite);
    }

    (void)pOutput;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return -1;

    int returnCode = -1;

    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            fprintf(stderr, "WSAStartup failed.\n");
            return returnCode;
        }
    #endif

    socket_t sockfd = INVALID_SOCKET;
    int rv = -1;
    struct addrinfo hints, *servInfo, *p;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servInfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        returnCode = -1;
        goto cleanup_info;
    }

    for (p = servInfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET)
        {
            PRINT_SOCK_ERROR("talker: socket");
            continue;
        }

        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket.\n");
        returnCode = -1;
        goto cleanup_sock;
    } 

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to initialize context.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    ma_device_info* pPlaybackInfos = NULL, *pCaptureInfos = NULL;
    ma_uint32 playbackCount = -1, captureCount = -1;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to get context devices.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice++)
        printf("Playback: %d - %s\n", iDevice, pPlaybackInfos[iDevice].name);

    for (ma_uint32 iDevice = 0; iDevice < captureCount; iDevice++)
        printf("Capture: %d - %s\n", iDevice, pCaptureInfos[iDevice].name);

    printf("Choose an id: ");
    int chosenID = -1;
    if ((scanf("%d", &chosenID) != 1) || chosenID < 0 || (ma_uint32)chosenID >= captureCount)
    {
        fprintf(stderr, "talker: invalid device ID.\n");
        returnCode = -1;
        goto cleanup_context;
    }

    printf("Chosen: %d\n", chosenID);
 
    ma_pcm_rb rb;
    if (ma_pcm_rb_init(FORMAT, CHANNELS, SAMPLE_RATE, NULL, NULL, &rb) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to initialize ring buffer.\n");
        returnCode = -1;
        goto cleanup_rb;
    }    

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = FORMAT;
    config.capture.channels = CHANNELS;
    config.capture.pDeviceID = &pCaptureInfos[chosenID].id;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = data_callback;
    config.pUserData = &rb; 
    
    ma_device device;
    if (ma_device_init(&context, &config, &device) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to initialize capture device.\n");
        returnCode = -2;
        goto cleanup_device;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        fprintf(stderr, "talker: failed to start capture device.\n");
        returnCode = -2;
        goto cleanup_device;
    }

    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    printf("Press any character to stop...\n");

    // maybe could use this in listener.c too
    char sendBuf[FRAMES_PER_PACKET * 8];
    size_t bytesPerFrame = ma_get_bytes_per_frame(FORMAT, CHANNELS);
    ma_uint32 framesBuffered = 0;
    while (1) 
    {   
        void *pBuffer;
        ma_uint32 framesToRead = FRAMES_PER_PACKET - framesBuffered;
        if (ma_pcm_rb_acquire_read(&rb, &framesToRead, &pBuffer) == MA_SUCCESS
            && framesToRead > 0)
        {
            memcpy(sendBuf + (framesBuffered * bytesPerFrame), pBuffer, framesToRead * bytesPerFrame);
            ma_pcm_rb_commit_read(&rb, framesToRead);
            framesBuffered += framesToRead;

            if (framesBuffered == FRAMES_PER_PACKET)
            {
                int bytesToSend = (int)(FRAMES_PER_PACKET * bytesPerFrame);

                if (sendto(sockfd, sendBuf, bytesToSend, 0, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR)
                {
                    PRINT_SOCK_ERROR("talker: sendto");
                    break;
                }

                framesBuffered = 0;
            }        
        }
        else
            ma_sleep(1);    
    }

    returnCode = 0;

    cleanup:
    cleanup_device:
        ma_device_uninit(&device);
    cleanup_rb:
        ma_pcm_rb_uninit(&rb);
    cleanup_context:
        ma_context_uninit(&context);
    cleanup_sock:
        CLOSE_SOCKET(sockfd);
    cleanup_info:
        if (servInfo)
            freeaddrinfo(servInfo); 

    #ifdef _WIN32
        WSACleanup();
    #endif

    return returnCode;
}

