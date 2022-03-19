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
#include "welle-cli/postgresinterface.h"


PostgresInterface::PostgresInterface(
                RadioControllerInterface& rci,
                int interval) :
    rci(rci),
    /* Connect to the PostgreSQL database */
    conn("host=localhost dbname=dabing user=pi password=slovensko")
{
    if (conn.is_open()) {
        std::cerr << "Database \"" << conn.dbname() << "\" successfully opened!" << std::endl;
        prepare_insert();
    }
    else {
        std::cerr << "Could not open database!" << std::endl;
    }

    period = interval;
    if (interval != -1) {
        running = true;
        /**
         * The thread is precisely
         * saving last data to the database.
         */
        th = std::thread(&PostgresInterface::run, this);
        // https://en.cppreference.com/w/cpp/thread/thread
    }
}

PostgresInterface::~PostgresInterface()
{
    /* Close connection */
    if (conn.is_open()) {
        conn.disconnect();
    }

    /* Stop thread */
    running = false;
    if (th.joinable()) {
        th.join();
    }
}

// Prepares "INSERT INTO" sql statement.
void PostgresInterface::prepare_insert()
{
    conn.prepare(
        "insert",
        "INSERT INTO dabing (snr,ber,power,fiber,signal,sync,tstz) VALUES ($1,$2,$3,$4,$5,$6,to_timestamp($7))"
    );
}

// Executes "INSERT INTO" sql statement.
void PostgresInterface::execute_insert(const dab_quality_indicators_t qi)
{
    try {
        pqxx::work t(conn);

        // we want to add time manually because
        // we cant be sure when will be data truly added to the table.
        // ("...tstz TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP(0)" sometimes leads to unwanted results)
        // timezone is set by psql automatically
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto tstz = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        // tstz is the number of seconds that have elapsed since January 1, 1970 (Unix epoch time)

        // Sequence of variables must match with prepared statement!
        t.exec_prepared("insert", qi.snr, qi.ber, qi.power, qi.fiber, qi.signal, qi.sync, tstz);

        // Not really needed, since we made no changes, but good habit to be
        // explicit about when the transaction is done.
        t.commit();
    }
    catch (std::exception const &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void PostgresInterface::run()
{
    using namespace std::chrono;

    // https://en.cppreference.com/w/cpp/chrono/time_point
    // https://en.cppreference.com/w/cpp/chrono/duration

    system_clock::time_point wait_until = system_clock::now();
    auto time_period = milliseconds(period);

    while (running) {
        wait_until += time_period;

        // Sleep until a specific point in time is reached
        std::this_thread::sleep_until(wait_until);

        //std::cout << duration_cast<milliseconds>(wait_until.time_since_epoch()).count() << std::endl;

        // Execute command
        execute_insert(rci.getQI());
    }

    std::clog << "postgresinterface: " <<  "closing down now" << std::endl;
}
