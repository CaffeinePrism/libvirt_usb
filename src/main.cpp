#include <libvirt/libvirt.h>
#include "pugixml.hpp"
#include <cstring>
#include <sstream>
#include <exception>
#include <iostream>
#include <regex>
#include <map>
#include <set>
#include <string>
typedef std::pair<std::string, std::string> ProductDescription;
typedef std::pair<unsigned int, unsigned int> ProductId;
typedef std::pair<ProductId, ProductDescription> ProductPair;
typedef std::map<ProductId, ProductDescription> USBDevices;
typedef std::set<ProductId> ProductIdSet;
bool name(pugi::xml_node& n) {
	std::cout << n.name() << std::endl;
	return n.attribute("hostdev").as_bool();
}

ProductIdSet getDomainInfo(std::string name) {
    virConnectPtr conn = NULL; /* the hypervisor connection */
    virDomainPtr dom = NULL;   /* the domain being checked */
    virDomainInfo info;        /* the information being fetched */
    int ret;
    ProductIdSet ret_devices;

	try {
		/* NULL means connect to local Xen hypervisor */
		conn = virConnectOpenReadOnly(NULL);
		if (conn == NULL) {
			std::ostringstream ess;
			ess	<< "Failed to connect to hypervisor" << std::endl;
			throw std::runtime_error(ess.str());
		}

		/* Find the domain of the given id */
		dom = virDomainLookupByName(conn, name.c_str());
		if (dom == NULL) {
			std::ostringstream ess;
			ess << "Failed to find Domain " << name << std::endl;
			throw std::runtime_error(ess.str());
		}

		/* Get the information */
		ret = virDomainGetInfo(dom, &info);
		if (ret < 0) {
			std::ostringstream ess;
			ess << "Failed to get information for Domain " << name << std::endl;
			throw std::runtime_error(ess.str());
		}
	}
	catch (std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		if (dom != NULL)
			virDomainFree(dom);
		if (conn != NULL)
			virConnectClose(conn);
		throw;
	}

	char* domxml = virDomainGetXMLDesc(dom, 0);

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(domxml);
	if (result) {
		for (pugi::xml_node n = doc.child("domain").child("devices").child("hostdev"); strcmp(n.name(), "hostdev") == 0; n = n.next_sibling())
		{
			if (strcmp(n.attribute("type").value(),"usb") == 0) {
                ProductId id = ProductId(n.child("source").child("vendor").attribute("id").as_uint(), n.child("source").child("product").attribute("id").as_uint());
                printf(">> 0x%04x:0x%04x\n", id.first, id.second);
				n.print(std::cout);
                ret_devices.insert(id);
			}
		}
	}
    if (dom != NULL)
        virDomainFree(dom);
    if (conn != NULL)
        virConnectClose(conn);
    return ret_devices;
}

USBDevices getHostUSBDevices() {
    virConnectPtr conn = NULL; /* the hypervisor connection */
    virNodeDevicePtr* dev = nullptr;        /* the information being fetched */
    int ret;
    USBDevices ret_devices;
    try {
        /* NULL means connect to local Xen hypervisor */
        conn = virConnectOpenReadOnly(NULL);
        if (conn == NULL) {
            std::ostringstream ess;
            ess	<< "Failed to connect to hypervisor" << std::endl;
            throw std::runtime_error(ess.str());
        }

        /* Find the domain of the given id */
        ret = virConnectListAllNodeDevices(conn, &dev, VIR_CONNECT_LIST_NODE_DEVICES_CAP_USB_DEV);

        std::regex base_regex("(usb_(?!usb)[_\\d]+)"); // ignore linux hubs (pattern is usb_usb*)
        std::cmatch match_results;
        for(int i = 0; i < ret; ++i) {
            const char* name = virNodeDeviceGetName(dev[i]);
            if (std::regex_match(name, match_results, base_regex)) {
                const char* xml = virNodeDeviceGetXMLDesc(dev[i], 0);
                pugi::xml_document doc;
            	pugi::xml_parse_result result = doc.load_string(xml);
                if (result) {
                    pugi::xml_node info = doc.child("device").child("capability");
                    ProductId ids = ProductId(info.child("vendor").attribute("id").as_uint(), info.child("product").attribute("id").as_uint());
                    ProductDescription description = ProductDescription(info.child("vendor").child_value(), info.child("product").child_value());
                    if (!((ids.first == 0x8087) && (ids.second > 0x8000))) { // Intel USB hubs
                        //printf("0x%04x:0x%04x :: %s, %s\n", ids.first, ids.second, description.first.c_str(), description.second.c_str());
                        ret_devices.insert(ProductPair(ids, description));
                    }
                }
            }
            virNodeDeviceFree(dev[i]);
        }
        delete[] dev;

    }
    catch (std::runtime_error& e) {
        std::cerr << e.what() << std::endl;
        if (conn != NULL)
            virConnectClose(conn);
        throw;
    }
    if (conn != NULL)
        virConnectClose(conn);
    return ret_devices;
}

int attachDevice(std::string domain, unsigned int vendor, unsigned int product) {
	virConnectPtr conn = NULL; /* the hypervisor connection */
	virDomainPtr dom = NULL;   /* the domain being checked */
	int ret = -1;
	try {
		/* NULL means connect to local Xen hypervisor */
		conn = virConnectOpen(NULL);
		if (conn == NULL) {
			std::ostringstream ess;
			ess	<< "Failed to connect to hypervisor" << std::endl;
			throw std::runtime_error(ess.str());
		}

		/* Find the domain of the given id */
		dom = virDomainLookupByName(conn, domain.c_str());
		if (dom == NULL) {
			std::ostringstream ess;
			ess << "Failed to find Domain " << domain << std::endl;
			throw std::runtime_error(ess.str());
		}
		std::string format_str = "<hostdev mode=\"subsystem\" type=\"usb\" managed=\"yes\"><source><vendor id=\"0x%04x\" /><product id=\"0x%04x\" /></source></hostdev>";
		int len = snprintf(NULL, 0, format_str.c_str(), vendor, product);
		char str[len];
		snprintf(str, len+1, format_str.c_str(), vendor, product);
		ret = virDomainAttachDeviceFlags(dom, str, VIR_DOMAIN_AFFECT_LIVE);
	}
	catch (std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		if (dom != NULL)
			virDomainFree(dom);
		if (conn != NULL)
			virConnectClose(conn);
		throw;
	}
	if (dom != NULL)
		virDomainFree(dom);
	if (conn != NULL)
		virConnectClose(conn);
	return ret;
}

int main(int argc, char** argv) {
    ProductIdSet attached_devices = getDomainInfo("win10");
    USBDevices devices = getHostUSBDevices();
    #define GREEN "\033[32m"
    #define RED "\033[31m"
    #define RESET "\033[0m"

    std::cout << RED << " + " << RESET << ": Device already attached to domain" << std::endl;
    std::cout << GREEN << " - " << RESET << ": Device not attached to domain" << std::endl << std::endl;
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        ProductId id = it->first;
        std::cout << RESET;
        if (attached_devices.find(id) != attached_devices.end()) {
            std::cout << RED << " + ";
        } else { std::cout << GREEN  << " - "; }
        printf("0x%04x:0x%04x :: %s, %s\n", id.first, id.second, it->second.first.c_str(), it->second.second.c_str());
    }
    std::cout << RESET;
    if (argc == 3) {
        attachDevice("win10", std::strtoul(argv[1], nullptr, 16), std::strtoul(argv[2], nullptr, 16));
    }
    return(0);
}
