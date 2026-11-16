#define MINIAUDIO_IMPLEMENTATION

#include <stdio.h>
#include "miniaudio.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

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
    
    int sockfd = -1, rv = -1, numBytes = -1;
    struct addrinfo hints, *servInfo, *p;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servInfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = servInfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("talker: socket");
            continue;
        }

        break;
    }
    
    if (p == NULL)
    {
        fprintf(stderr, "talker: failed to create socket.\n");
        return -1;
    } 
    
    ma_encoder_config encoderConfig;
    ma_encoder encoder;

    encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, FORMAT, CHANNELS, SAMPLE_RATE);

    if (ma_encoder_init_file(argv[1], &encoderConfig, &encoder) != MA_SUCCESS)
        return -1;

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS)
        return -1;

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
        return -1;

    for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice++)
        printf("Playback: %d - %s\n", iDevice, pPlaybackInfos[iDevice].name);

    for (ma_uint32 iDevice = 0; iDevice < captureCount; iDevice++)
        printf("Capture: %d - %s\n", iDevice, pCaptureInfos[iDevice].name);

    printf("Choose an id: ");
    int chosenID = -1;
    scanf("%d", &chosenID);

    if (chosenID < 0 || chosenID >= captureCount)
        return -1;

    printf("Chosen: %d\n", chosenID);
 
    ma_pcm_rb rb;
    if (ma_pcm_rb_init(FORMAT, CHANNELS, SAMPLE_RATE, NULL, NULL, &rb) != MA_SUCCESS)
        return -1;

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
        fprintf(stderr, "Failed to initialize capture device.\n");
        return -2;
    }

    if (ma_device_start(&device) != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        fprintf(stderr, "Failed to start capture device.\n");
        return -2;
    }

    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    printf("Press any character to stop...\n");
    while (1) 
    {   
        void *pBuffer;
        ma_uint32 framesToRead = FRAMES_PER_PACKET;
        if (ma_pcm_rb_acquire_read(&rb, &framesToRead, &pBuffer) == MA_SUCCESS
            && framesToRead > 0)
        {
            size_t bytesToSend = framesToRead * ma_get_bytes_per_frame(FORMAT, CHANNELS);

            if ((numBytes = sendto(sockfd, pBuffer, bytesToSend, 0, p->ai_addr, p->ai_addrlen)) == -1)
            {
                perror("talker: sendto");
                return -1;
            }            

            ma_pcm_rb_commit_read(&rb, framesToRead);
        }    
    }

    ma_device_uninit(&device);
    ma_context_uninit(&context);    
    ma_pcm_rb_uninit(&rb);
    ma_encoder_uninit(&encoder);
    close(sockfd);
    freeaddrinfo(servInfo);    

    return 0;
}

