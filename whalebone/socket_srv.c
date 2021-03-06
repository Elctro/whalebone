#include "socket_srv.h"
#include "thread_shared.h"

#include "crc64.h"
#include "file_loader.h"
#include "log.h"
#include "program.h"

void *connection_handler(void *socket_desc)
{
	//debugLog("\"method\":\"connection_handler\"");

	int sock = *(int*)socket_desc;
	int read_size;
	char client_message[4096];
	struct PrimeHeader primeHeader;
	struct MessageHeader messageHeader;
	int bytesRead = 0;

	char *bufferPtr = (char *)&primeHeader;
	while ((read_size = recv(sock, client_message, sizeof(struct PrimeHeader), 0)) > 0)
	{
		bytesRead += read_size;
		memcpy(bufferPtr, client_message, read_size);
		bufferPtr += read_size;
		if (read_size == -1 || read_size == 0 || bytesRead >= sizeof(struct PrimeHeader))
			break;
	}

	if (bytesRead == 0)
	{
		goto flush;
	}

	uint64_t crc = crc64(0, (const char *)&primeHeader, sizeof(struct PrimeHeader) - sizeof(uint64_t));
	sprintf(client_message, (primeHeader.headercrc == crc) ? "1" : "0");
	if (primeHeader.headercrc != crc)
	{
		goto flush;
	}

	for (int i = 0; i < primeHeader.buffercount; i++)
	{
		bufferPtr = (char *)&messageHeader;
		bytesRead = 0;
		while ((read_size = recv(sock, client_message, 16, 0)) > 0)
		{
			bytesRead += read_size;
			memcpy(bufferPtr, client_message, read_size);
			bufferPtr += read_size;
			if (read_size == -1 || read_size == 0 || bytesRead >= 16)
				break;
		}
		if (bytesRead == 0)
		{
			goto flush;
		}

		char *bufferMsg = (char *)calloc(1, messageHeader.length + 1);
		if (messageHeader.length == 0)
		{
			sprintf(client_message, "1");
		}
		else
		{
			if (bufferMsg == NULL)
			{
				return (void *)-1;
			}

			char *bufferMsgPtr = bufferMsg;
			bytesRead = 0;
			while ((read_size = recv(sock, client_message, 4096, 0)) > 0)
			{
				bytesRead += read_size;
				memcpy(bufferMsgPtr, client_message, read_size);
				bufferMsgPtr += read_size;

				if (read_size == -1 || read_size == 0 || bytesRead >= messageHeader.length)
					break;
			}
			crc = crc64(0, (const char *)bufferMsg, messageHeader.length);
			sprintf(client_message, (messageHeader.msgcrc == crc) ? "1" : "0");
			if (messageHeader.msgcrc != crc)
			{
				goto flush;
			}
		}

		debugLog("\"method\":\"connection_handler\",\"action\":\"%d\"", primeHeader.action);
		switch (primeHeader.action)
		{
			case Lmdb_customlists:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_domains, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_domains:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_customlists, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_matrix:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_matrix, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_policy:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_policies, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_ranges:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_ranges, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_radius:
			{
				char *file = (char *)bufferMsg;
				load_lmdb(env_radius, file);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
			case Lmdb_cloudgroup:
			{
				char *path = (char *)bufferMsg;
				load_lmdbs(path);

				if (bufferMsg)
				{
					free(bufferMsg);
					bufferMsg = NULL;
				}
				break;
			}
		}
	}

flush:

	close(sock);
	free(socket_desc);

	return 0;
}

void* socket_server(void *arg)
{
	int socket_desc, new_socket, c, *new_sock;
	struct sockaddr_in server, client;

	//Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		debugLog("\"method\":\"socket_server\",\"message\":\"Could not create socket\"");
	}

	//Prepare the sockaddr_in structure
	server.sin_family = AF_INET;
	if (getenv("SOCKET_SRV_IPv4") != NULL)
	{
		server.sin_addr.s_addr = inet_addr(getenv("SOCKET_SRV_IPv4"));
	}
	else
	{
		server.sin_addr.s_addr = inet_addr("127.0.0.1");
	}
	
	for (int port = 8880; port < 9048; port++)
	{
		server.sin_port = htons(port);
		char message[255] = {};
		//Bind
		if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
		{
			debugLog("\"method\":\"socket_server\",\"message\":\"bind failed on port %d\"", port);
			if (port == 9048)
			{
				return (void*)-1;
			}

			continue;
		}
		debugLog("\"method\":\"socket_server\",\"message\":\"bind succeeded on port %d\"", port);
		break;
	}

	//Listen
	listen(socket_desc, 3);

	//Accept and incoming connection
	debugLog("\"method\":\"socket_server\",\"message\":\"waiting for incoming connections\"");
	c = sizeof(struct sockaddr_in);
	while ((new_socket = accept(socket_desc, (struct sockaddr *)&client, (socklen_t*)&c)))
	{
		//logDebug("connection accepted");

		pthread_t sniffer_thread;
		new_sock = malloc(1);
		*new_sock = new_socket;

		if (pthread_create(&sniffer_thread, NULL, connection_handler, (void*)new_sock) < 0)
		{
			debugLog("\"method\":\"socket_server\",\"message\":\"could not create thread\"");
			return (void*)-1;
		}

		pthread_join(sniffer_thread, NULL);
		//logDebug("handler assigned");
	}

	if (new_socket < 0)
	{
		debugLog("\"method\":\"socket_server\",\"message\":\"accept failed\"");
		return (void*)-1;
	}

	return 0;
}

void send_message(int logyype, const char *message)
{
	int rc = 0;
	switch (logyype)
	{
		case log_debug:
			rc = sendto(socket_debug, message, strlen(message), 0 ,(struct sockaddr *) &si_debug, sizeof(si_debug));
			break;
		case log_audit:
			rc = sendto(socket_threat, message, strlen(message), 0 ,(struct sockaddr *) &si_threat, sizeof(si_threat));
			break;
		case log_content:
			rc = sendto(socket_content, message, strlen(message), 0 ,(struct sockaddr *) &si_content, sizeof(si_content));
			break;
		default:
			break;
	}
	
	if (rc == -1)
	{
		fprintf(stderr, "sendto() failed %s", message);
	}
}