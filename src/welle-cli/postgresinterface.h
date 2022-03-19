/*
 *    Copyright (C) 2022
 *    Jakub Svajka
 *
 *    This file is extension of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */
#ifndef __POSTGRES_INTERFACE
#define __POSTGRES_INTERFACE

#include "backend/radio-controller.h"
#include <iostream>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <pqxx/pqxx>

class PostgresInterface {
    public:
        PostgresInterface(
                RadioControllerInterface& rci,
                int interval);

        ~PostgresInterface();
        void prepare_insert();
        void execute_insert(const dab_quality_indicators_t qi);
        void run();
    private:
        RadioControllerInterface& rci;
        pqxx::connection conn;

        std::atomic<bool> running = ATOMIC_VAR_INIT(false);
        std::thread th;

        int period;
};

#endif