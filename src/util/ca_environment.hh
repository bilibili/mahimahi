#ifndef CERTIFICATEENV_HH
#define CERTIFICATEENV_HH

#include <string>

class CAEnvironment
{
private:
	void copyFile(std::string from, std::string to);
public:
	std::string dirname;

    CAEnvironment(const std::string mahimahi_root);
    ~CAEnvironment();
};


#endif