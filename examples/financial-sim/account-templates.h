#pragma once

#include "chart-of-accounts.h"
#include <vector>

// Account template factory for different business types
class AccountTemplateFactory {
public:
    // Create a retail business template
    static AccountTemplate create_retail_template() {
        AccountTemplate tmpl("retail", "Standard retail business accounts", "retail");
        
        // Retail-specific assets
        tmpl.accounts.push_back(Account("1150", "Merchandise Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1151", "Inventory - Electronics", AccountType::ASSET, "1150", 3));
        tmpl.accounts.push_back(Account("1152", "Inventory - Clothing", AccountType::ASSET, "1150", 3));
        tmpl.accounts.push_back(Account("1153", "Inventory - Home Goods", AccountType::ASSET, "1150", 3));
        tmpl.accounts.push_back(Account("1160", "Store Fixtures", AccountType::ASSET, "1200", 2));
        tmpl.accounts.push_back(Account("1161", "Point of Sale Systems", AccountType::ASSET, "1200", 2));
        
        // Retail-specific revenue
        tmpl.accounts.push_back(Account("4110", "Retail Sales", AccountType::REVENUE, "4100", 2));
        tmpl.accounts.push_back(Account("4120", "Online Sales", AccountType::REVENUE, "4100", 2));
        tmpl.accounts.push_back(Account("4500", "Sales Returns and Allowances", AccountType::REVENUE, "4000", 1));
        
        // Retail-specific expenses
        tmpl.accounts.push_back(Account("5110", "Store Rent", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5120", "Store Utilities", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5130", "Sales Staff Salaries", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5140", "Advertising and Promotions", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Create a manufacturing template
    static AccountTemplate create_manufacturing_template() {
        AccountTemplate tmpl("manufacturing", "Manufacturing business accounts", "manufacturing");
        
        // Manufacturing-specific assets
        tmpl.accounts.push_back(Account("1160", "Raw Materials Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1170", "Work-in-Process Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1180", "Finished Goods Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1210", "Manufacturing Equipment", AccountType::ASSET, "1200", 2));
        tmpl.accounts.push_back(Account("1211", "Production Machinery", AccountType::ASSET, "1210", 3));
        tmpl.accounts.push_back(Account("1212", "Assembly Line Equipment", AccountType::ASSET, "1210", 3));
        tmpl.accounts.push_back(Account("1220", "Factory Buildings", AccountType::ASSET, "1200", 2));
        
        // Manufacturing-specific expenses
        tmpl.accounts.push_back(Account("5210", "Direct Materials", AccountType::EXPENSE, "5200", 2));
        tmpl.accounts.push_back(Account("5220", "Direct Labor", AccountType::EXPENSE, "5200", 2));
        tmpl.accounts.push_back(Account("5230", "Manufacturing Overhead", AccountType::EXPENSE, "5200", 2));
        tmpl.accounts.push_back(Account("5231", "Factory Utilities", AccountType::EXPENSE, "5230", 3));
        tmpl.accounts.push_back(Account("5232", "Factory Supplies", AccountType::EXPENSE, "5230", 3));
        tmpl.accounts.push_back(Account("5233", "Maintenance and Repairs", AccountType::EXPENSE, "5230", 3));
        
        return tmpl;
    }
    
    // Create a services template
    static AccountTemplate create_services_template() {
        AccountTemplate tmpl("services", "Professional services business accounts", "services");
        
        // Services-specific assets
        tmpl.accounts.push_back(Account("1120", "Unbilled Receivables", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1130", "Office Equipment", AccountType::ASSET, "1200", 2));
        tmpl.accounts.push_back(Account("1140", "Computer Systems", AccountType::ASSET, "1200", 2));
        
        // Services-specific revenue
        tmpl.accounts.push_back(Account("4210", "Consulting Revenue", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4220", "Professional Fees", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4230", "Billable Hours", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4240", "Retainer Fees", AccountType::REVENUE, "4200", 2));
        
        // Services-specific expenses
        tmpl.accounts.push_back(Account("5150", "Professional Development", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5160", "Subcontractor Costs", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5170", "Client Entertainment", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5180", "Office Rent", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Create a restaurant template
    static AccountTemplate create_restaurant_template() {
        AccountTemplate tmpl("restaurant", "Restaurant business accounts", "restaurant");
        
        // Restaurant-specific assets
        tmpl.accounts.push_back(Account("1160", "Food Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1161", "Beverage Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1162", "Supplies Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1210", "Kitchen Equipment", AccountType::ASSET, "1200", 2));
        tmpl.accounts.push_back(Account("1211", "Restaurant Furniture", AccountType::ASSET, "1200", 2));
        
        // Restaurant-specific revenue
        tmpl.accounts.push_back(Account("4110", "Food Sales", AccountType::REVENUE, "4100", 2));
        tmpl.accounts.push_back(Account("4120", "Beverage Sales", AccountType::REVENUE, "4100", 2));
        tmpl.accounts.push_back(Account("4130", "Catering Revenue", AccountType::REVENUE, "4100", 2));
        
        // Restaurant-specific expenses
        tmpl.accounts.push_back(Account("5210", "Food Costs", AccountType::EXPENSE, "5200", 2));
        tmpl.accounts.push_back(Account("5220", "Beverage Costs", AccountType::EXPENSE, "5200", 2));
        tmpl.accounts.push_back(Account("5150", "Kitchen Staff Wages", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5151", "Service Staff Wages", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Create a technology/SaaS template
    static AccountTemplate create_saas_template() {
        AccountTemplate tmpl("saas", "Software-as-a-Service business accounts", "technology");
        
        // SaaS-specific assets
        tmpl.accounts.push_back(Account("1310", "Software Development Costs", AccountType::ASSET, "1300", 2));
        tmpl.accounts.push_back(Account("1320", "Customer Acquisition Costs", AccountType::ASSET, "1300", 2));
        tmpl.accounts.push_back(Account("1140", "Servers and Infrastructure", AccountType::ASSET, "1200", 2));
        
        // SaaS-specific revenue
        tmpl.accounts.push_back(Account("4210", "Subscription Revenue", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4211", "Monthly Subscriptions", AccountType::REVENUE, "4210", 3));
        tmpl.accounts.push_back(Account("4212", "Annual Subscriptions", AccountType::REVENUE, "4210", 3));
        tmpl.accounts.push_back(Account("4220", "Professional Services", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4230", "Setup Fees", AccountType::REVENUE, "4200", 2));
        
        // SaaS-specific expenses
        tmpl.accounts.push_back(Account("5150", "Cloud Hosting Costs", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5160", "Software Licenses", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5170", "Developer Salaries", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5180", "Customer Support", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5190", "Sales and Marketing", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Create a real estate template
    static AccountTemplate create_real_estate_template() {
        AccountTemplate tmpl("real_estate", "Real estate business accounts", "real_estate");
        
        // Real estate-specific assets
        tmpl.accounts.push_back(Account("1210", "Investment Properties", AccountType::ASSET, "1200", 2));
        tmpl.accounts.push_back(Account("1211", "Residential Properties", AccountType::ASSET, "1210", 3));
        tmpl.accounts.push_back(Account("1212", "Commercial Properties", AccountType::ASSET, "1210", 3));
        tmpl.accounts.push_back(Account("1220", "Property Under Development", AccountType::ASSET, "1200", 2));
        
        // Real estate-specific revenue
        tmpl.accounts.push_back(Account("4410", "Rental Income - Residential", AccountType::REVENUE, "4400", 2));
        tmpl.accounts.push_back(Account("4420", "Rental Income - Commercial", AccountType::REVENUE, "4400", 2));
        tmpl.accounts.push_back(Account("4430", "Property Management Fees", AccountType::REVENUE, "4400", 2));
        tmpl.accounts.push_back(Account("4440", "Late Fees", AccountType::REVENUE, "4400", 2));
        
        // Real estate-specific expenses
        tmpl.accounts.push_back(Account("5150", "Property Taxes", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5160", "Property Insurance", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5170", "Property Maintenance", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5180", "Property Management Fees", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Create a healthcare template
    static AccountTemplate create_healthcare_template() {
        AccountTemplate tmpl("healthcare", "Healthcare provider accounts", "healthcare");
        
        // Healthcare-specific assets
        tmpl.accounts.push_back(Account("1120", "Patient Receivables", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1121", "Insurance Claims Receivable", AccountType::ASSET, "1120", 3));
        tmpl.accounts.push_back(Account("1160", "Medical Supplies Inventory", AccountType::ASSET, "1100", 2));
        tmpl.accounts.push_back(Account("1210", "Medical Equipment", AccountType::ASSET, "1200", 2));
        
        // Healthcare-specific revenue
        tmpl.accounts.push_back(Account("4210", "Patient Services Revenue", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4211", "Outpatient Services", AccountType::REVENUE, "4210", 3));
        tmpl.accounts.push_back(Account("4212", "Inpatient Services", AccountType::REVENUE, "4210", 3));
        tmpl.accounts.push_back(Account("4220", "Pharmacy Revenue", AccountType::REVENUE, "4200", 2));
        tmpl.accounts.push_back(Account("4230", "Laboratory Revenue", AccountType::REVENUE, "4200", 2));
        
        // Healthcare-specific expenses
        tmpl.accounts.push_back(Account("5150", "Medical Staff Salaries", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5160", "Medical Supplies", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5170", "Pharmaceutical Costs", AccountType::EXPENSE, "5100", 2));
        tmpl.accounts.push_back(Account("5180", "Medical Malpractice Insurance", AccountType::EXPENSE, "5100", 2));
        
        return tmpl;
    }
    
    // Get all available templates
    static std::vector<AccountTemplate> get_all_templates() {
        return {
            create_retail_template(),
            create_manufacturing_template(),
            create_services_template(),
            create_restaurant_template(),
            create_saas_template(),
            create_real_estate_template(),
            create_healthcare_template()
        };
    }
};
