#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <functional>
#include <fstream>
#include <limits>
#include <fstream>

// Modern C++ Hotel Management System with SQLite Database

// Forward declarations
class Item;
class InventoryManager;
class OrderManager;
class ReportManager;
class UserManager;
class Database;

// Database singleton class
class Database {
private:
    sqlite3* db;
    static Database* instance;
    
    Database() : db(nullptr) {}
    
public:
    static Database& getInstance() {
        if (!instance) {
            instance = new Database();
        }
        return *instance;
    }
    
    bool connect(const std::string& dbName = "hotel.db") {
        int rc = sqlite3_open(dbName.c_str(), &db);
        if (rc) {
            std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }
        
        initializeTables();
        return true;
    }
    
    bool executeQuery(const std::string& query) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &errMsg);
        
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }
    
    // Callback for select queries
    using ResultCallback = std::function<void(int, char**, char**)>;
    
    bool executeSelect(const std::string& query, ResultCallback callback) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db, query.c_str(), 
            [](void* data, int argc, char** argv, char** azColName) -> int {
                ResultCallback* cb = static_cast<ResultCallback*>(data);
                if (cb) (*cb)(argc, argv, azColName);
                return 0;
            }, &callback, &errMsg);
        
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }
    
    void close() {
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
    
    ~Database() {
        close();
    }
    
private:
    void initializeTables() {
        // Create users table
        executeQuery("CREATE TABLE IF NOT EXISTS users ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "username TEXT UNIQUE NOT NULL,"
                    "password TEXT NOT NULL,"
                    "role TEXT NOT NULL,"
                    "created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");
        
        // Create inventory table
        executeQuery("CREATE TABLE IF NOT EXISTS inventory ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "name TEXT UNIQUE NOT NULL,"
                    "price INTEGER NOT NULL,"
                    "quantity INTEGER NOT NULL,"
                    "category TEXT NOT NULL)");
        
        // Create sales table
        executeQuery("CREATE TABLE IF NOT EXISTS sales ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "item_id INTEGER NOT NULL,"
                    "quantity INTEGER NOT NULL,"
                    "total_price INTEGER NOT NULL,"
                    "user_id INTEGER NOT NULL,"
                    "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                    "FOREIGN KEY (item_id) REFERENCES inventory(id),"
                    "FOREIGN KEY (user_id) REFERENCES users(id))");
        
        // Insert default admin if not exists
        executeQuery("INSERT OR IGNORE INTO users (username, password, role) VALUES ('admin', 'admin123', 'admin')");
        
        // Insert default inventory items if not exists
        executeQuery("INSERT OR IGNORE INTO inventory (name, price, quantity, category) VALUES "
                   "('Room', 1200, 10, 'accommodation'),"
                   "('Pasta', 250, 50, 'food'),"
                   "('Burger', 120, 50, 'food'),"
                   "('Noodles', 140, 50, 'food'),"
                   "('Shake', 120, 50, 'drink'),"
                   "('Chicken Roll', 150, 50, 'food')");
    }
};

// Initialize static member
Database* Database::instance = nullptr;

// Helper function for string escaping to prevent SQL injection
std::string sqlEscape(const std::string& input) {
    std::string result = input;
    size_t pos = 0;
    while ((pos = result.find("'", pos)) != std::string::npos) {
        result.replace(pos, 1, "''");
        pos += 2;
    }
    return result;
}

// Item class (represents a product or service)
class Item {
private:
    int id;
    std::string name;
    int price;
    std::string category;

public:
    Item(int id, const std::string& name, int price, const std::string& category) 
        : id(id), name(name), price(price), category(category) {}
    
    int getId() const { return id; }
    std::string getName() const { return name; }
    int getPrice() const { return price; }
    std::string getCategory() const { return category; }
};

// InventoryManager class
class InventoryManager {
public:
    static std::vector<Item> getAllItems() {
        std::vector<Item> items;
        
        Database::getInstance().executeSelect(
            "SELECT id, name, price, category FROM inventory ORDER BY category, name",
            [&items](int argc, char** argv, char** azColName) {
                if (argc >= 4) {
                    int id = std::stoi(argv[0]);
                    std::string name = argv[1];
                    int price = std::stoi(argv[2]);
                    std::string category = argv[3];
                    
                    items.emplace_back(id, name, price, category);
                }
            }
        );
        
        return items;
    }
    
    static Item getItemById(int id) {
        Item item(0, "", 0, "");
        
        Database::getInstance().executeSelect(
            "SELECT id, name, price, category FROM inventory WHERE id = " + std::to_string(id),
            [&item](int argc, char** argv, char** azColName) {
                if (argc >= 4) {
                    int id = std::stoi(argv[0]);
                    std::string name = argv[1];
                    int price = std::stoi(argv[2]);
                    std::string category = argv[3];
                    
                    item = Item(id, name, price, category);
                }
            }
        );
        
        return item;
    }
    
    static int getQuantity(int itemId) {
        int quantity = 0;
        
        Database::getInstance().executeSelect(
            "SELECT quantity FROM inventory WHERE id = " + std::to_string(itemId),
            [&quantity](int argc, char** argv, char** azColName) {
                if (argc >= 1) {
                    quantity = std::stoi(argv[0]);
                }
            }
        );
        
        return quantity;
    }
    
    static bool updateQuantity(int itemId, int newQuantity) {
        std::string query = "UPDATE inventory SET quantity = " + 
                          std::to_string(newQuantity) + 
                          " WHERE id = " + std::to_string(itemId);
        
        return Database::getInstance().executeQuery(query);
    }
    
    static bool decreaseQuantity(int itemId, int amount) {
        int currentQty = getQuantity(itemId);
        
        if (currentQty >= amount) {
            return updateQuantity(itemId, currentQty - amount);
        }
        
        return false;
    }
};

// OrderManager class
class OrderManager {
public:
    static bool processOrder(int itemId, int quantity, int userId) {
        Item item = InventoryManager::getItemById(itemId);
        int currentQty = InventoryManager::getQuantity(itemId);
        
        if (currentQty < quantity) {
            std::cout << "\nNot enough inventory. Only " << currentQty << " available." << std::endl;
            return false;
        }
        
        int totalPrice = item.getPrice() * quantity;
        
        // Begin transaction
        Database::getInstance().executeQuery("BEGIN TRANSACTION");
        
        bool success = true;
        
        // Update inventory
        if (!InventoryManager::decreaseQuantity(itemId, quantity)) {
            success = false;
        }
        
        // Record sale
        std::string saleQuery = "INSERT INTO sales (item_id, quantity, total_price, user_id) VALUES (" +
                              std::to_string(itemId) + ", " +
                              std::to_string(quantity) + ", " +
                              std::to_string(totalPrice) + ", " +
                              std::to_string(userId) + ")";
        
        if (!Database::getInstance().executeQuery(saleQuery)) {
            success = false;
        }
        
        // Commit or rollback
        if (success) {
            Database::getInstance().executeQuery("COMMIT");
            
            // Display order confirmation
            std::cout << "\n\n\t\t" << quantity << " " << item.getName();
            
            if (item.getCategory() == "accommodation") {
                std::cout << "(s) have been allotted to you";
            } else {
                std::cout << " is the order!";
            }
            
            // Show bill
            std::cout << "\n\n Bill details:";
            std::cout << "\n Item: " << item.getName();
            std::cout << "\n Quantity: " << quantity; 
            std::cout << "\n Price per item: $" << item.getPrice();
            std::cout << "\n Total: $" << totalPrice << std::endl;
            
            return true;
        } else {
            Database::getInstance().executeQuery("ROLLBACK");
            return false;
        }
    }
};

// ReportManager class
class ReportManager {
public:
    static void displayDailySales() {
        std::cout << "\n\tDetails of Sales and Collection\n";
        std::cout << "\n------------------------------------------------------";
        std::cout << "\nItem                 Quantity Sold    Total Revenue";
        std::cout << "\n------------------------------------------------------";
        
        int totalRevenue = 0;
        
        Database::getInstance().executeSelect(
            "SELECT i.name, i.category, SUM(s.quantity) as qty_sold, SUM(s.total_price) as revenue "
            "FROM sales s "
            "JOIN inventory i ON s.item_id = i.id "
            "WHERE DATE(s.timestamp) = DATE('now') "
            "GROUP BY s.item_id "
            "ORDER BY i.category, i.name",
            [&totalRevenue](int argc, char** argv, char** azColName) {
                if (argc >= 4) {
                    std::string name = argv[0];
                    int qtySold = std::stoi(argv[2]);
                    int revenue = std::stoi(argv[3]);
                    
                    std::cout << "\n" << std::left << std::setw(20) << name 
                             << std::right << std::setw(10) << qtySold
                             << std::setw(15) << "$" << revenue;
                    
                    totalRevenue += revenue;
                }
            }
        );
        
        std::cout << "\n------------------------------------------------------";
        std::cout << "\nTotal Revenue:                          $" << totalRevenue;
        std::cout << "\n------------------------------------------------------\n";
    }
    
    static void displayInventoryStatus() {
        std::cout << "\n\tCurrent Inventory Status\n";
        std::cout << "\n------------------------------------------------------";
        std::cout << "\nItem                 Price    Available    Category";
        std::cout << "\n------------------------------------------------------";
        
        Database::getInstance().executeSelect(
            "SELECT name, price, quantity, category FROM inventory ORDER BY category, name",
            [](int argc, char** argv, char** azColName) {
                if (argc >= 4) {
                    std::string name = argv[0];
                    int price = std::stoi(argv[1]);
                    int quantity = std::stoi(argv[2]);
                    std::string category = argv[3];
                    
                    std::cout << "\n" << std::left << std::setw(20) << name 
                             << std::right << std::setw(5) << "$" << price
                             << std::setw(12) << quantity
                             << std::setw(12) << category;
                }
            }
        );
        
        std::cout << "\n------------------------------------------------------\n";
    }
    
    static void resetDailySales() {
        std::cout << "\nDo you want to archive today's sales data? (y/n): ";
        char choice;
        std::cin >> choice;
        
        if (choice == 'y' || choice == 'Y') {
            // Export today's sales to CSV
            exportSalesReport();
            
            // Don't actually delete the sales data from database
            // This just archives it to CSV for reporting purposes
            std::cout << "\nSales data has been archived successfully!" << std::endl;
        }
    }
    
private:
    static void exportSalesReport() {
        // Get current date for filename
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        
        char dateStr[20];
        std::strftime(dateStr, sizeof(dateStr), "%Y%m%d", localTime);
        
        std::string filename = "sales_report_" + std::string(dateStr) + ".csv";
        
        std::ofstream report(filename);
        if (!report) {
            std::cerr << "Error: Unable to create report file!" << std::endl;
            return;
        }
        
        // Write CSV header
        report << "Date,Item,Category,Quantity,Unit Price,Total Price,User\n";
        
        // Query and write sales data
        Database::getInstance().executeSelect(
            "SELECT s.timestamp, i.name, i.category, s.quantity, i.price, s.total_price, u.username "
            "FROM sales s "
            "JOIN inventory i ON s.item_id = i.id "
            "JOIN users u ON s.user_id = u.id "
            "WHERE DATE(s.timestamp) = DATE('now') "
            "ORDER BY s.timestamp",
            [&report](int argc, char** argv, char** azColName) {
                if (argc >= 7) {
                    report << argv[0] << "," // timestamp
                          << argv[1] << "," // item name
                          << argv[2] << "," // category
                          << argv[3] << "," // quantity
                          << argv[4] << "," // unit price
                          << argv[5] << "," // total price
                          << argv[6] << "\n"; // username
                }
            }
        );
        
        report.close();
        std::cout << "\nSales report exported to " << filename << std::endl;
    }
};

// UserManager class
class UserManager {
public:
    static int authenticateUser(const std::string& username, const std::string& password) {
        int userId = -1;
        
        // Escape input strings to prevent SQL injection
        std::string safeUsername = sqlEscape(username);
        std::string safePassword = sqlEscape(password);
        
        Database::getInstance().executeSelect(
            "SELECT id FROM users WHERE username = '" + safeUsername + "' AND password = '" + safePassword + "'",
            [&userId](int argc, char** argv, char** azColName) {
                if (argc >= 1) {
                    userId = std::stoi(argv[0]);
                }
            }
        );
        
        return userId;
    }
    
    static std::string getUserRole(int userId) {
        std::string role = "";
        
        Database::getInstance().executeSelect(
            "SELECT role FROM users WHERE id = " + std::to_string(userId),
            [&role](int argc, char** argv, char** azColName) {
                if (argc >= 1) {
                    role = argv[0];
                }
            }
        );
        
        return role;
    }
    
    static bool addUser(const std::string& username, const std::string& password, const std::string& role) {
        // Escape input strings to prevent SQL injection
        std::string safeUsername = sqlEscape(username);
        std::string safePassword = sqlEscape(password);
        std::string safeRole = sqlEscape(role);
        
        std::string query = "INSERT INTO users (username, password, role) VALUES ('" +
                          safeUsername + "', '" + safePassword + "', '" + safeRole + "')";
        
        return Database::getInstance().executeQuery(query);
    }
};

// Application class (main controller)
class HotelApp {
private:
    int currentUserId;
    std::string currentUserRole;
    
public:
    HotelApp() : currentUserId(-1) {}
    
    bool initialize() {
        std::cout << "\n\t\t\t=================================================";
        std::cout << "\n\t\t\t|        HOTEL MANAGEMENT SYSTEM                |";
        std::cout << "\n\t\t\t=================================================";
        
        // Connect to database
        if (!Database::getInstance().connect()) {
            std::cerr << "Failed to initialize database!" << std::endl;
            return false;
        }
        
        // Login
        return login();
    }
    
    bool login() {
        std::string username, password;
        int attempts = 0;
        
        while (attempts < 3) {
            std::cout << "\n\n=== LOGIN ===";
            std::cout << "\nUsername: ";
            std::cin >> username;
            std::cout << "Password: ";
            std::cin >> password;
            
            currentUserId = UserManager::authenticateUser(username, password);
            
            if (currentUserId > 0) {
                currentUserRole = UserManager::getUserRole(currentUserId);
                std::cout << "\nLogin successful! Welcome, " << username << "!";
                return true;
            } else {
                attempts++;
                std::cout << "\nInvalid username or password. Attempts remaining: " << (3 - attempts);
            }
        }
        
        std::cout << "\nToo many failed attempts. Exiting program...";
        return false;
    }
    
    void run() {
        int choice;
        
        while (true) {
            displayMenu();
            
            // Get user choice with error handling
            if (!(std::cin >> choice)) {
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                choice = -1;  // Invalid input
            }
            
            // Process menu choice
            if (processMenuChoice(choice)) {
                break;  // Exit program
            }
            
            std::cout << "\n\nPress Enter to continue...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cin.get();
        }
    }
    
private:
    void displayMenu() {
        std::vector<Item> items = InventoryManager::getAllItems();
        
        std::cout << "\n\n\t\t\t Please select from the menu options ";
        
        int menuIndex = 1;
        
        // Display inventory items
        for (const auto& item : items) {
            std::cout << "\n" << menuIndex++ << ") " << item.getName() 
                     << " - $" << item.getPrice();
        }
        
        // Display admin options
        std::cout << "\n" << menuIndex++ << ") View sales report";
        std::cout << "\n" << menuIndex++ << ") View inventory status";
        
        if (currentUserRole == "admin") {
            std::cout << "\n" << menuIndex++ << ") Reset daily sales";
            std::cout << "\n" << menuIndex++ << ") Add new user";
        }
        
        std::cout << "\n" << menuIndex << ") Exit";
        std::cout << "\n\nPlease Enter your choice: ";
    }
    
    bool processMenuChoice(int choice) {
        std::vector<Item> items = InventoryManager::getAllItems();
        
        // Handle item purchases (1 to items.size())
        if (choice >= 1 && choice <= static_cast<int>(items.size())) {
            int index = choice - 1;
            int quantity;
            
            std::cout << "\n\nEnter " << items[index].getName() << " quantity: ";
            std::cin >> quantity;
            
            if (quantity > 0) {
                OrderManager::processOrder(items[index].getId(), quantity, currentUserId);
            } else {
                std::cout << "\nInvalid quantity!";
            }
            
            return false;  // Don't exit
        }
        
        // Handle special options
        int specialOptionStart = items.size() + 1;
        
        if (choice == specialOptionStart) {
            // View sales report
            ReportManager::displayDailySales();
        } 
        else if (choice == specialOptionStart + 1) {
            // View inventory status
            ReportManager::displayInventoryStatus();
        }
        else if (currentUserRole == "admin" && choice == specialOptionStart + 2) {
            // Reset daily sales (admin only)
            ReportManager::resetDailySales();
        }
        else if (currentUserRole == "admin" && choice == specialOptionStart + 3) {
            // Add new user (admin only)
            addNewUser();
        }
        else if ((currentUserRole == "admin" && choice == specialOptionStart + 4) ||
                 (currentUserRole != "admin" && choice == specialOptionStart + 2)) {
            // Exit
            std::cout << "\nExiting program...";
            return true;
        }
        else {
            std::cout << "\nPlease select a valid option!";
        }
        
        return false;
    }
    
    void addNewUser() {
        std::string username, password, role;
        
        std::cout << "\n=== Add New User ===";
        std::cout << "\nUsername: ";
        std::cin >> username;
        std::cout << "Password: ";
        std::cin >> password;
        std::cout << "Role (admin/staff): ";
        std::cin >> role;
        
        if (role != "admin" && role != "staff") {
            std::cout << "\nInvalid role! Using 'staff' as default.";
            role = "staff";
        }
        
        if (UserManager::addUser(username, password, role)) {
            std::cout << "\nUser added successfully!";
        } else {
            std::cout << "\nFailed to add user. Username may already exist.";
        }
    }
};

int main() {
    HotelApp app;
    
    if (app.initialize()) {
        app.run();
    }
    
    return 0;
}