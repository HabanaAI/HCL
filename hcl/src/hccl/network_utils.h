/******************************************************************************
 * Copyright (C) 2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string
#include <vector>   // for vector

struct sockaddr_storage;

// A result of function detect_tcp_if(), storing TCP network interface name and its IP address.
struct detected_tcp_if
{
    std::string if_name;
    std::string ip_addr;
};

// Determines the TCP network interface and its IP address to be used for server socket creation.
detected_tcp_if detect_tcp_if();

// Determines the TCP network interfaces and their IP address to be used for server socket creation.
int detect_tcp_ifs(std::vector<detected_tcp_if>& detected_tcp_ifs);

// Translate address from sockaddr in human readable string.
std::string address_to_string(const sockaddr_storage* addr);

std::string get_global_comm_id();

std::string get_global_comm_ip();

int get_global_comm_port();

std::string get_desired_tcp_if_from_env_var();

void parse_user_tcp_ifs(std::string ifs_list, std::vector<std::string>& parsed_ifs_list);

bool match_tcp_if_pattern(const std::string& tcp_if_name, const std::vector<std::string>& ifs);

bool ip_is_local(const std::string ip);

// Receives the data by reading the socket.
// Works similarly to recv(..., MSG_WAITALL), but guarantees to return one of the following:
//  * Negative integer - when an unrecoverable error occurs, or a message has been read partially.
//  * 0 - when the socket is closed (remote peer closed the connection prior to this call).
//  * Positive integer - in this case it is always `length`.
// This function will never return value >0 but less than `length`.
int recv_all(int sockfd, void* buffer, size_t length);
