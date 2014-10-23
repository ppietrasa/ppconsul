//  Copyright (c) 2014 Andrey Upadyshev <oliora@gmail.com>
//
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <catch/catch.hpp>

#include "ppconsul/catalog.h"
#include "ppconsul/agent.h"
#include "test_consul.h"
#include <algorithm>
#include <thread>


using ppconsul::catalog::Catalog;
using ppconsul::agent::Agent;
namespace params = ppconsul::params;
using ppconsul::Consistency;


namespace {
    const auto Uniq_Name_1 = "B0D8A57F-0A73-4C6A-926A-262088B00B76";
    const auto Uniq_Name_2 = "749E5A49-4202-4995-AD5B-A3F28E19ADC1";
    const auto Uniq_Name_1_Spec = "\r\nB0D8A57F-0A73-4C6A-926A-262088B00B76{}";
    const auto Uniq_Name_2_Spec = "{}749E5A49-4202-4995-AD5B-A3F28E19ADC1\r\n";
    const auto Tag_Spec = "{}bla\r\n";
    const auto Non_Existing_Service_Name = "D0087276-8F85-4612-AC88-8871DB15B2A7";
    const auto Non_Existing_Tag_Name = Non_Existing_Service_Name;
    const auto Non_Existing_Node_Name = Non_Existing_Service_Name;

    inline void sleep(double seconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0)));
    }
}

TEST_CASE("catalog.node_valid", "[consul][catalog][config]")
{
    CHECK_FALSE((ppconsul::catalog::Node{}).valid());
    CHECK((ppconsul::catalog::Node{ "name", "addr" }).valid());
    CHECK_FALSE((ppconsul::catalog::Node{ "", "addr" }).valid());
    CHECK_FALSE((ppconsul::catalog::Node{ "name", "" }).valid());
}


TEST_CASE("catalog.datacenters", "[consul][catalog][config]")
{
    auto consul = create_test_consul();
    Catalog catalog(consul);

    auto dcs = catalog.datacenters();

    REQUIRE(dcs.size() > 0);
    CHECK(std::find(dcs.begin(), dcs.end(), get_test_datacenter()) != dcs.end());

    for (const auto& d : dcs)
    {
        CHECK(d != "");
    }
}

TEST_CASE("catalog.nodes", "[consul][catalog][config]")
{
    auto consul = create_test_consul();
    Catalog catalog(consul);

    const auto selfMember = Agent(consul).self().second;

    auto nodes = catalog.nodes();

    REQUIRE(nodes.size() > 0);

    auto it1 = std::find_if(nodes.begin(), nodes.end(), [&](const ppconsul::catalog::Node& op){
        return op.address == selfMember.address;
    });

    REQUIRE(it1 != nodes.end());
    CHECK((it1->name == selfMember.name
        || it1->name.find(selfMember.name + ".") == 0));

    for (const auto& node : nodes)
    {
        CHECK(node.name != "");
        CHECK(node.address != "");
    }
}

TEST_CASE("catalog.services", "[consul][catalog][services]")
{
    auto consul = create_test_consul();
    Catalog catalog(consul);
    Agent agent(consul);

    const auto selfMember = Agent(consul).self().second;
    const auto selfNode = ppconsul::catalog::Node{ selfMember.name, selfMember.address };

    agent.deregisterService("service1");
    agent.deregisterService("service2");
    agent.deregisterService("service3");
    agent.registerService({ Uniq_Name_1, 1234, { "print", "udp" }, "service1" });
    agent.registerService({ Uniq_Name_2, 2345, { "copier", "udp" }, "service2" });
    agent.registerService({ Uniq_Name_1, 3456, { "print", "secret" }, "service3" });

    sleep(1); // Give some time to propogate registered services to the catalog

    SECTION("services")
    {
        auto services = catalog.services();

        REQUIRE(services.count(Uniq_Name_1));
        REQUIRE(services.count(Uniq_Name_2));

        CHECK(services.at(Uniq_Name_1) == ppconsul::Tags({ "print", "secret", "udp" }));
        CHECK(services.at(Uniq_Name_2) == ppconsul::Tags({ "copier", "udp" }));
    }

    SECTION("non existing service")
    {
        CHECK(catalog.service(Non_Existing_Service_Name).empty());
    }

    SECTION("service")
    {
        auto services = catalog.service(Uniq_Name_1);
        
        REQUIRE(services.size() == 2);

        const auto service1Index = services[0].first.id == "service1" ? 0 : 1;

        CHECK(services[service1Index].second == selfNode);
        CHECK(services[service1Index].first.name == Uniq_Name_1);
        CHECK(services[service1Index].first.port == 1234);
        CHECK(services[service1Index].first.tags == ppconsul::Tags({ "print", "udp" }));
        CHECK(services[service1Index].first.id == "service1");

        CHECK(services[1 - service1Index].second == selfNode);
        CHECK(services[1 - service1Index].first.name == Uniq_Name_1);
        CHECK(services[1 - service1Index].first.port == 3456);
        CHECK(services[1 - service1Index].first.tags == ppconsul::Tags({ "print", "secret" }));
        CHECK(services[1 - service1Index].first.id == "service3");
    }

    SECTION("non existing service tag")
    {
        CHECK(catalog.service(Uniq_Name_1, Non_Existing_Tag_Name).empty());
    }

    SECTION("service with tag")
    {
        auto services1 = catalog.service(Uniq_Name_1, "udp");

        REQUIRE(services1.size() == 1);

        CHECK(services1[0].second == selfNode);
        CHECK(services1[0].first.name == Uniq_Name_1);
        CHECK(services1[0].first.port == 1234);
        CHECK(services1[0].first.tags == ppconsul::Tags({ "print", "udp" }));
        CHECK(services1[0].first.id == "service1");

        auto services2 = catalog.service(Uniq_Name_1, "print");

        REQUIRE(services2.size() == 2);

        const auto service1Index = services2[0].first.id == "service1" ? 0 : 1;
        
        CHECK(services2[service1Index].second == selfNode);
        CHECK(services2[service1Index].first.name == Uniq_Name_1);
        CHECK(services2[service1Index].first.port == 1234);
        CHECK(services2[service1Index].first.tags == ppconsul::Tags({ "print", "udp" }));
        CHECK(services2[service1Index].first.id == "service1");
        
        CHECK(services2[1 - service1Index].second == selfNode);
        CHECK(services2[1 - service1Index].first.name == Uniq_Name_1);
        CHECK(services2[1 - service1Index].first.port == 3456);
        CHECK(services2[1 - service1Index].first.tags == ppconsul::Tags({ "print", "secret" }));
        CHECK(services2[1 - service1Index].first.id == "service3");
    }

    SECTION("node")
    {
        auto node = catalog.node(selfMember.name);

        REQUIRE(node.first.valid());
        CHECK(node.first == selfNode);
        
        REQUIRE(node.second.count("service1"));
        REQUIRE(node.second.count("service2"));
        REQUIRE(node.second.count("service3"));

        const auto& s1 = node.second.at("service1");
        const auto& s2 = node.second.at("service2");
        const auto& s3 = node.second.at("service3");
        
        CHECK(s1.name == Uniq_Name_1);
        CHECK(s1.port == 1234);
        CHECK(s1.tags == ppconsul::Tags({ "print", "udp" }));
        CHECK(s1.id == "service1");

        CHECK(s2.name == Uniq_Name_2);
        CHECK(s2.port == 2345);
        CHECK(s2.tags == ppconsul::Tags({ "copier", "udp" }));
        CHECK(s2.id == "service2");

        CHECK(s3.name == Uniq_Name_1);
        CHECK(s3.port == 3456);
        CHECK(s3.tags == ppconsul::Tags({ "print", "secret" }));
        CHECK(s3.id == "service3");
    }

    SECTION("non-existing node")
    {
        auto node = catalog.node(Non_Existing_Node_Name);

        CHECK_FALSE(node.first.valid());
        CHECK(node.second.empty());
    }
}

TEST_CASE("catalog.services_special_chars", "[consul][catalog][services][special chars]")
{
    auto consul = create_test_consul();
    Catalog catalog(consul);
    Agent agent(consul);

    const auto selfMember = Agent(consul).self().second;
    const auto selfNode = ppconsul::catalog::Node{ selfMember.name, selfMember.address };

    agent.deregisterService("service1");
    agent.deregisterService("service2");
    agent.deregisterService("service3");
    agent.registerService({ Uniq_Name_1_Spec, 1234, { "print", Tag_Spec }, "service1" });
    agent.registerService({ Uniq_Name_2_Spec, 2345, { "copier", Tag_Spec }, "service2" });
    agent.registerService({ Uniq_Name_1_Spec, 3456, { "print", "secret" }, "service3" });

    sleep(1); // Give some time to propogate registered services to the catalog

    SECTION("services")
    {
        auto services = catalog.services();

        REQUIRE(services.count(Uniq_Name_1_Spec));
        REQUIRE(services.count(Uniq_Name_2_Spec));

        CHECK(services.at(Uniq_Name_1_Spec) == ppconsul::Tags({ "print", "secret", Tag_Spec }));
        CHECK(services.at(Uniq_Name_2_Spec) == ppconsul::Tags({ "copier", Tag_Spec }));
    }

    SECTION("service")
    {
        auto services = catalog.service(Uniq_Name_1_Spec);

        REQUIRE(services.size() == 2);

        const auto service1Index = services[0].first.id == "service1" ? 0 : 1;

        CHECK(services[service1Index].second == selfNode);
        CHECK(services[service1Index].first.name == Uniq_Name_1_Spec);
        CHECK(services[service1Index].first.port == 1234);
        CHECK(services[service1Index].first.tags == ppconsul::Tags({ "print", Tag_Spec }));
        CHECK(services[service1Index].first.id == "service1");

        CHECK(services[1 - service1Index].second == selfNode);
        CHECK(services[1 - service1Index].first.name == Uniq_Name_1_Spec);
        CHECK(services[1 - service1Index].first.port == 3456);
        CHECK(services[1 - service1Index].first.tags == ppconsul::Tags({ "print", "secret" }));
        CHECK(services[1 - service1Index].first.id == "service3");
    }

    SECTION("service with tag")
    {
        auto services1 = catalog.service(Uniq_Name_1_Spec, Tag_Spec);

        REQUIRE(services1.size() == 1);

        CHECK(services1[0].second == selfNode);
        CHECK(services1[0].first.name == Uniq_Name_1_Spec);
        CHECK(services1[0].first.port == 1234);
        CHECK(services1[0].first.tags == ppconsul::Tags({ "print", Tag_Spec }));
        CHECK(services1[0].first.id == "service1");

        auto services2 = catalog.service(Uniq_Name_1_Spec, "print");

        REQUIRE(services2.size() == 2);

        const auto service1Index = services2[0].first.id == "service1" ? 0 : 1;

        CHECK(services2[service1Index].second == selfNode);
        CHECK(services2[service1Index].first.name == Uniq_Name_1_Spec);
        CHECK(services2[service1Index].first.port == 1234);
        CHECK(services2[service1Index].first.tags == ppconsul::Tags({ "print", Tag_Spec }));
        CHECK(services2[service1Index].first.id == "service1");
        
        CHECK(services2[1 - service1Index].second == selfNode);
        CHECK(services2[1 - service1Index].first.name == Uniq_Name_1_Spec);
        CHECK(services2[1 - service1Index].first.port == 3456);
        CHECK(services2[1 - service1Index].first.tags == ppconsul::Tags({ "print", "secret" }));
        CHECK(services2[1 - service1Index].first.id == "service3");
    }
}