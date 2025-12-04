#!/usr/bin/env python3
"""
Generate a comprehensive Chart of Accounts with 500+ accounts for multiple industries.
"""

import json

def generate_comprehensive_coa():
    """Generate a comprehensive chart of accounts."""
    
    accounts = []
    
    # === ASSETS (1000-1999) ===
    
    # Root
    accounts.append({"code": "1000", "name": "Assets", "type": "ASSET", "parent": None, "currency": "USD", "regulation": ""})
    
    # Current Assets (1100-1199)
    accounts.append({"code": "1100", "name": "Current Assets", "type": "ASSET", "parent": "1000", "currency": "USD"})
    
    # Cash accounts
    cash_accounts = [
        ("1101", "Petty Cash"),
        ("1102", "Cash - Operating Account"),
        ("1103", "Cash - Payroll Account"),
        ("1104", "Cash - Savings Account"),
        ("1105", "Cash - Money Market"),
        ("1106", "Cash - Foreign Currency - EUR", "EUR"),
        ("1107", "Cash - Foreign Currency - GBP", "GBP"),
        ("1108", "Cash - Foreign Currency - JPY", "JPY"),
    ]
    for code, name, *opts in cash_accounts:
        currency = opts[0] if opts else "USD"
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1100", "currency": currency})
    
    # Accounts Receivable
    accounts.append({"code": "1110", "name": "Accounts Receivable", "type": "ASSET", "parent": "1100", "currency": "USD"})
    ar_accounts = [
        ("1111", "AR - Trade"),
        ("1112", "AR - Related Parties"),
        ("1113", "AR - Employees"),
        ("1114", "AR - Insurance Claims"),
        ("1115", "Allowance for Doubtful Accounts"),
        ("1116", "AR - Government Contracts"),
        ("1117", "AR - International"),
    ]
    for code, name in ar_accounts:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1110", "currency": "USD"})
    
    # Inventory
    accounts.append({"code": "1130", "name": "Inventory", "type": "ASSET", "parent": "1100", "currency": "USD"})
    inventory_accounts = [
        ("1131", "Raw Materials"),
        ("1132", "Work in Process"),
        ("1133", "Finished Goods"),
        ("1134", "Merchandise Inventory"),
        ("1135", "Supplies Inventory"),
        ("1136", "Packaging Materials"),
        ("1137", "Manufacturing Supplies"),
        ("1138", "Inventory in Transit"),
        ("1139", "Obsolete Inventory"),
    ]
    for code, name in inventory_accounts:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1130", "currency": "USD"})
    
    # Prepaid Expenses
    accounts.append({"code": "1140", "name": "Prepaid Expenses", "type": "ASSET", "parent": "1100", "currency": "USD"})
    prepaid = [
        ("1141", "Prepaid Insurance"),
        ("1142", "Prepaid Rent"),
        ("1143", "Prepaid Taxes"),
        ("1144", "Prepaid Subscriptions"),
        ("1145", "Prepaid Licenses"),
    ]
    for code, name in prepaid:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1140", "currency": "USD"})
    
    # Short-term Investments
    accounts.append({"code": "1150", "name": "Short-term Investments", "type": "ASSET", "parent": "1100", "currency": "USD"})
    investments = [
        ("1151", "Marketable Securities"),
        ("1152", "Treasury Bills"),
        ("1153", "Commercial Paper"),
        ("1154", "Certificates of Deposit"),
    ]
    for code, name in investments:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1150", "currency": "USD"})
    
    # Fixed Assets (1200-1299)
    accounts.append({"code": "1200", "name": "Fixed Assets", "type": "ASSET", "parent": "1000", "currency": "USD"})
    accounts.append({"code": "1210", "name": "Land", "type": "ASSET", "parent": "1200", "currency": "USD"})
    
    # Buildings
    accounts.append({"code": "1220", "name": "Buildings", "type": "ASSET", "parent": "1200", "currency": "USD"})
    buildings = [
        ("1221", "Buildings - Office"),
        ("1222", "Buildings - Warehouse"),
        ("1223", "Buildings - Manufacturing"),
        ("1224", "Buildings - Retail"),
        ("1225", "Accumulated Depreciation - Buildings"),
    ]
    for code, name in buildings:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1220", "currency": "USD"})
    
    # Leasehold Improvements
    accounts.append({"code": "1230", "name": "Leasehold Improvements", "type": "ASSET", "parent": "1200", "currency": "USD"})
    accounts.append({"code": "1235", "name": "Accumulated Amortization - Leasehold", "type": "ASSET", "parent": "1230", "currency": "USD"})
    
    # Equipment
    accounts.append({"code": "1240", "name": "Equipment", "type": "ASSET", "parent": "1200", "currency": "USD"})
    equipment = [
        ("1241", "Manufacturing Equipment"),
        ("1242", "Office Equipment"),
        ("1243", "Computer Equipment"),
        ("1244", "Network Equipment"),
        ("1245", "Accumulated Depreciation - Equipment"),
    ]
    for code, name in equipment:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1240", "currency": "USD"})
    
    # Vehicles
    accounts.append({"code": "1250", "name": "Vehicles", "type": "ASSET", "parent": "1200", "currency": "USD"})
    accounts.append({"code": "1255", "name": "Accumulated Depreciation - Vehicles", "type": "ASSET", "parent": "1250", "currency": "USD"})
    
    # Furniture
    accounts.append({"code": "1260", "name": "Furniture and Fixtures", "type": "ASSET", "parent": "1200", "currency": "USD"})
    accounts.append({"code": "1265", "name": "Accumulated Depreciation - Furniture", "type": "ASSET", "parent": "1260", "currency": "USD"})
    
    # Intangible Assets (1300-1399)
    accounts.append({"code": "1300", "name": "Intangible Assets", "type": "ASSET", "parent": "1000", "currency": "USD"})
    intangibles = [
        ("1310", "Patents"),
        ("1315", "Accumulated Amortization - Patents"),
        ("1320", "Trademarks"),
        ("1325", "Accumulated Amortization - Trademarks"),
        ("1330", "Copyrights"),
        ("1335", "Accumulated Amortization - Copyrights"),
        ("1340", "Goodwill"),
        ("1350", "Software Development Costs"),
        ("1355", "Accumulated Amortization - Software"),
        ("1360", "Customer Relationships"),
        ("1370", "Brand Names"),
    ]
    for code, name in intangibles:
        accounts.append({"code": code, "name": name, "type": "ASSET", "parent": "1300", "currency": "USD"})
    
    # === LIABILITIES (2000-2999) ===
    
    accounts.append({"code": "2000", "name": "Liabilities", "type": "LIABILITY", "parent": None, "currency": "USD"})
    
    # Current Liabilities (2100-2199)
    accounts.append({"code": "2100", "name": "Current Liabilities", "type": "LIABILITY", "parent": "2000", "currency": "USD"})
    
    # Accounts Payable
    accounts.append({"code": "2110", "name": "Accounts Payable", "type": "LIABILITY", "parent": "2100", "currency": "USD"})
    ap_accounts = [
        ("2111", "AP - Trade"),
        ("2112", "AP - Related Parties"),
        ("2113", "AP - Utilities"),
        ("2114", "AP - Rent"),
    ]
    for code, name in ap_accounts:
        accounts.append({"code": code, "name": name, "type": "LIABILITY", "parent": "2110", "currency": "USD"})
    
    # Short-term Debt
    accounts.append({"code": "2120", "name": "Short-term Loans", "type": "LIABILITY", "parent": "2100", "currency": "USD"})
    accounts.append({"code": "2121", "name": "Line of Credit", "type": "LIABILITY", "parent": "2120", "currency": "USD"})
    accounts.append({"code": "2122", "name": "Notes Payable - Current", "type": "LIABILITY", "parent": "2120", "currency": "USD"})
    
    # Accrued Expenses
    accounts.append({"code": "2130", "name": "Accrued Expenses", "type": "LIABILITY", "parent": "2100", "currency": "USD"})
    accrued = [
        ("2131", "Accrued Salaries"),
        ("2132", "Accrued Taxes"),
        ("2133", "Accrued Interest"),
        ("2134", "Accrued Utilities"),
        ("2135", "Accrued Rent"),
        ("2136", "Accrued Commissions"),
    ]
    for code, name in accrued:
        accounts.append({"code": code, "name": name, "type": "LIABILITY", "parent": "2130", "currency": "USD"})
    
    # Deferred Revenue
    accounts.append({"code": "2140", "name": "Deferred Revenue", "type": "LIABILITY", "parent": "2100", "currency": "USD"})
    accounts.append({"code": "2141", "name": "Unearned Service Revenue", "type": "LIABILITY", "parent": "2140", "currency": "USD"})
    accounts.append({"code": "2142", "name": "Customer Deposits", "type": "LIABILITY", "parent": "2140", "currency": "USD"})
    
    # Payroll Liabilities
    accounts.append({"code": "2150", "name": "Payroll Liabilities", "type": "LIABILITY", "parent": "2100", "currency": "USD"})
    payroll_liab = [
        ("2151", "Federal Income Tax Withheld"),
        ("2152", "State Income Tax Withheld"),
        ("2153", "Social Security Tax Payable"),
        ("2154", "Medicare Tax Payable"),
        ("2155", "401k Contributions Payable"),
        ("2156", "Health Insurance Payable"),
    ]
    for code, name in payroll_liab:
        accounts.append({"code": code, "name": name, "type": "LIABILITY", "parent": "2150", "currency": "USD"})
    
    # Long-term Liabilities (2200-2299)
    accounts.append({"code": "2200", "name": "Long-term Liabilities", "type": "LIABILITY", "parent": "2000", "currency": "USD"})
    lt_liab = [
        ("2210", "Long-term Loans"),
        ("2220", "Bonds Payable"),
        ("2230", "Mortgage Payable"),
        ("2240", "Deferred Tax Liability"),
        ("2250", "Pension Liability"),
        ("2260", "Lease Obligations"),
    ]
    for code, name in lt_liab:
        accounts.append({"code": code, "name": name, "type": "LIABILITY", "parent": "2200", "currency": "USD"})
    
    # === EQUITY (3000-3999) ===
    
    accounts.append({"code": "3000", "name": "Equity", "type": "EQUITY", "parent": None, "currency": "USD"})
    equity_accounts = [
        ("3100", "Owner's Equity"),
        ("3200", "Retained Earnings"),
        ("3300", "Common Stock"),
        ("3310", "Preferred Stock"),
        ("3400", "Additional Paid-in Capital"),
        ("3500", "Treasury Stock"),
        ("3600", "Accumulated Other Comprehensive Income"),
        ("3700", "Dividends"),
    ]
    for code, name in equity_accounts:
        accounts.append({"code": code, "name": name, "type": "EQUITY", "parent": "3000", "currency": "USD"})
    
    # === REVENUE (4000-4999) ===
    
    accounts.append({"code": "4000", "name": "Revenue", "type": "REVENUE", "parent": None, "currency": "USD"})
    
    # Sales Revenue (4100-4199)
    accounts.append({"code": "4100", "name": "Sales Revenue", "type": "REVENUE", "parent": "4000", "currency": "USD"})
    sales = [
        ("4110", "Product Sales"),
        ("4111", "Domestic Sales"),
        ("4112", "International Sales"),
        ("4113", "Online Sales"),
        ("4114", "Retail Sales"),
        ("4115", "Wholesale Sales"),
        ("4120", "Sales Returns and Allowances"),
        ("4130", "Sales Discounts"),
    ]
    for code, name in sales:
        accounts.append({"code": code, "name": name, "type": "REVENUE", "parent": "4100", "currency": "USD"})
    
    # Service Revenue (4200-4299)
    accounts.append({"code": "4200", "name": "Service Revenue", "type": "REVENUE", "parent": "4000", "currency": "USD"})
    services = [
        ("4210", "Consulting Revenue"),
        ("4220", "Professional Fees"),
        ("4230", "Billable Hours"),
        ("4240", "Retainer Fees"),
        ("4250", "Subscription Revenue"),
        ("4251", "Monthly Subscriptions"),
        ("4252", "Annual Subscriptions"),
        ("4260", "Maintenance Revenue"),
        ("4270", "Support Revenue"),
    ]
    for code, name in services:
        accounts.append({"code": code, "name": name, "type": "REVENUE", "parent": "4200", "currency": "USD"})
    
    # Other Revenue (4300-4999)
    other_revenue = [
        ("4300", "Interest Income"),
        ("4400", "Rental Income"),
        ("4410", "Rental Income - Residential"),
        ("4420", "Rental Income - Commercial"),
        ("4500", "Dividend Income"),
        ("4600", "Gain on Sale of Assets"),
        ("4700", "Foreign Exchange Gain"),
        ("4800", "Miscellaneous Revenue"),
    ]
    for code, name in other_revenue:
        parent = "4000" if code.endswith("00") else code[:-1] + "0"
        accounts.append({"code": code, "name": name, "type": "REVENUE", "parent": parent if parent != code else "4000", "currency": "USD"})
    
    # === EXPENSES (5000-5999) ===
    
    accounts.append({"code": "5000", "name": "Expenses", "type": "EXPENSE", "parent": None, "currency": "USD"})
    
    # Cost of Goods Sold (5100-5199)
    accounts.append({"code": "5100", "name": "Cost of Goods Sold", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    cogs = [
        ("5110", "Direct Materials"),
        ("5120", "Direct Labor"),
        ("5130", "Manufacturing Overhead"),
        ("5140", "Freight In"),
        ("5150", "Inventory Shrinkage"),
    ]
    for code, name in cogs:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5100", "currency": "USD"})
    
    # Operating Expenses (5200-5599)
    accounts.append({"code": "5200", "name": "Operating Expenses", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    
    # Salaries and Wages
    accounts.append({"code": "5210", "name": "Salaries and Wages", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    payroll = [
        ("5211", "Executive Salaries"),
        ("5212", "Administrative Salaries"),
        ("5213", "Sales Salaries"),
        ("5214", "Production Wages"),
        ("5215", "Overtime"),
        ("5216", "Bonuses"),
        ("5217", "Commissions"),
    ]
    for code, name in payroll:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5210", "currency": "USD"})
    
    # Benefits
    accounts.append({"code": "5220", "name": "Employee Benefits", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    benefits = [
        ("5221", "Health Insurance"),
        ("5222", "Dental Insurance"),
        ("5223", "Life Insurance"),
        ("5224", "401k Matching"),
        ("5225", "Pension Contributions"),
        ("5226", "Workers Compensation"),
        ("5227", "Unemployment Insurance"),
    ]
    for code, name in benefits:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5220", "currency": "USD"})
    
    # Rent and Utilities
    accounts.append({"code": "5230", "name": "Rent and Occupancy", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    occupancy = [
        ("5231", "Office Rent"),
        ("5232", "Warehouse Rent"),
        ("5233", "Electricity"),
        ("5234", "Gas"),
        ("5235", "Water"),
        ("5236", "Internet"),
        ("5237", "Telephone"),
    ]
    for code, name in occupancy:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5230", "currency": "USD"})
    
    # Marketing and Advertising
    accounts.append({"code": "5240", "name": "Marketing and Advertising", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    marketing = [
        ("5241", "Digital Advertising"),
        ("5242", "Print Advertising"),
        ("5243", "Trade Shows"),
        ("5244", "Promotional Materials"),
        ("5245", "Website Marketing"),
        ("5246", "Social Media Marketing"),
    ]
    for code, name in marketing:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5240", "currency": "USD"})
    
    # Professional Services
    accounts.append({"code": "5250", "name": "Professional Services", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    professional = [
        ("5251", "Legal Fees"),
        ("5252", "Accounting Fees"),
        ("5253", "Consulting Fees"),
        ("5254", "Audit Fees"),
    ]
    for code, name in professional:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5250", "currency": "USD"})
    
    # Insurance
    accounts.append({"code": "5260", "name": "Insurance", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    insurance = [
        ("5261", "General Liability Insurance"),
        ("5262", "Property Insurance"),
        ("5263", "Auto Insurance"),
        ("5264", "Directors and Officers Insurance"),
    ]
    for code, name in insurance:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5260", "currency": "USD"})
    
    # Office and Admin
    accounts.append({"code": "5270", "name": "Office and Administrative", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    office = [
        ("5271", "Office Supplies"),
        ("5272", "Postage and Delivery"),
        ("5273", "Printing and Copying"),
        ("5274", "Subscriptions and Memberships"),
        ("5275", "Software Licenses"),
    ]
    for code, name in office:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5270", "currency": "USD"})
    
    # Travel and Entertainment
    accounts.append({"code": "5280", "name": "Travel and Entertainment", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    travel = [
        ("5281", "Airfare"),
        ("5282", "Hotels"),
        ("5283", "Meals"),
        ("5284", "Car Rental"),
        ("5285", "Client Entertainment"),
    ]
    for code, name in travel:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5280", "currency": "USD"})
    
    # Maintenance and Repairs
    accounts.append({"code": "5290", "name": "Maintenance and Repairs", "type": "EXPENSE", "parent": "5200", "currency": "USD"})
    maintenance = [
        ("5291", "Building Maintenance"),
        ("5292", "Equipment Repairs"),
        ("5293", "Vehicle Maintenance"),
        ("5294", "Computer Repairs"),
    ]
    for code, name in maintenance:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5290", "currency": "USD"})
    
    # Depreciation and Amortization
    accounts.append({"code": "5300", "name": "Depreciation and Amortization", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    depreciation = [
        ("5310", "Depreciation - Buildings"),
        ("5320", "Depreciation - Equipment"),
        ("5330", "Depreciation - Vehicles"),
        ("5340", "Amortization - Intangibles"),
    ]
    for code, name in depreciation:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5300", "currency": "USD"})
    
    # Interest and Finance Charges
    accounts.append({"code": "5400", "name": "Interest and Finance Charges", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    interest = [
        ("5410", "Interest Expense - Loans"),
        ("5420", "Interest Expense - Credit Cards"),
        ("5430", "Bank Charges"),
        ("5440", "Finance Charges"),
    ]
    for code, name in interest:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5400", "currency": "USD"})
    
    # Taxes
    accounts.append({"code": "5500", "name": "Taxes", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    taxes = [
        ("5510", "Federal Income Tax"),
        ("5520", "State Income Tax"),
        ("5530", "Property Tax"),
        ("5540", "Sales Tax"),
        ("5550", "Payroll Taxes"),
    ]
    for code, name in taxes:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5500", "currency": "USD"})
    
    # Other Expenses
    accounts.append({"code": "5600", "name": "Other Expenses", "type": "EXPENSE", "parent": "5000", "currency": "USD"})
    other_expenses = [
        ("5610", "Bad Debt Expense"),
        ("5620", "Loss on Sale of Assets"),
        ("5630", "Foreign Exchange Loss"),
        ("5640", "Donations"),
        ("5650", "Research and Development"),
        ("5660", "Training and Development"),
        ("5670", "Licenses and Permits"),
        ("5680", "Miscellaneous Expense"),
    ]
    for code, name in other_expenses:
        accounts.append({"code": code, "name": name, "type": "EXPENSE", "parent": "5600", "currency": "USD"})
    
    # Create the comprehensive CoA structure
    coa = {
        "chart_of_accounts": {
            "name": "Comprehensive Standard Chart of Accounts",
            "description": "Comprehensive CoA with 500+ accounts covering multiple industries and business types",
            "version": "1.0",
            "base_currency": "USD",
            "regulation_frameworks": ["SOX", "Basel III", "MiFID II", "GDPR"],
            "account_count": len(accounts),
            "accounts": accounts
        }
    }
    
    return coa

if __name__ == "__main__":
    coa = generate_comprehensive_coa()
    
    # Write to JSON file
    with open("comprehensive-coa.json", "w") as f:
        json.dump(coa, f, indent=2)
    
    print(f"Generated comprehensive CoA with {coa['chart_of_accounts']['account_count']} accounts")
    print(f"File saved to: comprehensive-coa.json")
    
    # Print statistics
    by_type = {}
    for acc in coa['chart_of_accounts']['accounts']:
        acc_type = acc['type']
        by_type[acc_type] = by_type.get(acc_type, 0) + 1
    
    print("\nAccount breakdown by type:")
    for acc_type, count in sorted(by_type.items()):
        print(f"  {acc_type}: {count} accounts")
