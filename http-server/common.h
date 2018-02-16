/*
 * Copyright (c) 2017 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "config.h"

#include <icnet/icnet_http_facade.h>
#include <icnet/icnet_utils_hash.h>

#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/functional/hash.hpp>
#include <memory>
#include <algorithm>

#include <unordered_map>
#include <thread>
#include <future>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

typedef boost::asio::ip::tcp::socket socket_type;
typedef std::function<void(const boost::system::error_code &)> SendCallback;
