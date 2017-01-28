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

class LibvirtWrapper {
    public:
        LibvirtWrapper(std::string domain_name) {
            std::ostringstream ess;

            connection = virConnectOpen(NULL);
            if (connection == NULL) {
                ess	<< "Failed to connect to hypervisor" << std::endl;
                throw std::runtime_error(ess.str());
            }

            /* Find the domain of the given id */
            domain = virDomainLookupByName(connection, domain_name.c_str());
            if (domain == NULL) {
                ess << "Failed to find Domain " << domain_name << std::endl;
                if (connection != NULL) {
                    virConnectClose(connection);
                }
                throw std::runtime_error(ess.str());
            }
        }

        ~LibvirtWrapper() {
            if (domain != NULL) {
                virDomainFree(domain);
            }
    		if (connection != NULL) {
                virConnectClose(connection);
            }
        }

        virConnectPtr getConnectPtr() { return connection; }
        virDomainPtr getDomainPtr() { return domain; }

    private:
        virConnectPtr connection = NULL;
        virDomainPtr domain = NULL;
};

ProductIdSet getDomainInfo(virDomainPtr dom) {
    virDomainInfo info;        /* the information being fetched */
    int ret;
    ProductIdSet ret_devices;

	ret = virDomainGetInfo(dom, &info);
	if (ret < 0) {
		std::ostringstream ess;
		ess << "Failed to get information for Domain " << std::endl;
		throw std::runtime_error(ess.str());
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

    return ret_devices;
}

USBDevices getHostUSBDevices(virConnectPtr conn) {
    virNodeDevicePtr* dev = nullptr;        /* the information being fetched */
    int ret;
    USBDevices ret_devices;

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
    free(dev);

    return ret_devices;
}

std::string buildDeviceString(unsigned int vendor, unsigned int product) {
    std::string format_str = "<hostdev mode=\"subsystem\" type=\"usb\" managed=\"yes\"><source><vendor id=\"0x%04x\" /><product id=\"0x%04x\" /></source></hostdev>";
    char str[128]; // len should only be 123 chars
    sprintf(str, format_str.c_str(), vendor, product);

    return std::string(str);
}

int attachDevice(virDomainPtr dom, unsigned int vendor, unsigned int product) {
	int ret = -1;
	ret = virDomainAttachDeviceFlags(dom, buildDeviceString(vendor, product).c_str(), VIR_DOMAIN_AFFECT_LIVE);

	return ret;
}

int detachDevice(virDomainPtr dom, unsigned int vendor, unsigned int product) {
	int ret = -1;
	ret = virDomainDetachDeviceFlags(dom, buildDeviceString(vendor, product).c_str(), VIR_DOMAIN_AFFECT_LIVE);

	return ret;
}

int main(int argc, char** argv) {
    LibvirtWrapper wrapper{"win10"};
    virConnectPtr conn = wrapper.getConnectPtr();
    virDomainPtr dom = wrapper.getDomainPtr();

    ProductIdSet attached_devices = getDomainInfo(dom);
    USBDevices devices = getHostUSBDevices(conn);
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
        int ret = attachDevice(dom, std::strtoul(argv[1], nullptr, 16), std::strtoul(argv[2], nullptr, 16));
        std::cout << "attachDevice returned: " << ret << std::endl;
    }
    return(0);
}
