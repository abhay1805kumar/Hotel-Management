import streamlit as st
import sqlite3
import pandas as pd
import hashlib
from datetime import datetime, date
import csv
import io

# Page configuration
st.set_page_config(
    page_title="Hotel Management System",
    page_icon="🏨",
    layout="wide",
    initial_sidebar_state="expanded"
)

# Database helper functions
def init_database():
    """Initialize the SQLite database with required tables"""
    conn = sqlite3.connect('hotel.db')
    cursor = conn.cursor()
    
    # Create users table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            role TEXT NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # Create inventory table  
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS inventory (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            price INTEGER NOT NULL,
            quantity INTEGER NOT NULL,
            category TEXT NOT NULL
        )
    ''')
    
    # Create sales table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sales (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            item_id INTEGER NOT NULL,
            quantity INTEGER NOT NULL,
            total_price INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (item_id) REFERENCES inventory(id),
            FOREIGN KEY (user_id) REFERENCES users(id)
        )
    ''')
    
    # Insert default admin if not exists
    cursor.execute("SELECT COUNT(*) FROM users WHERE username = 'admin'")
    if cursor.fetchone()[0] == 0:
        admin_password = hashlib.md5('admin123'.encode()).hexdigest()
        cursor.execute("INSERT INTO users (username, password, role) VALUES (?, ?, ?)",
                      ('admin', admin_password, 'admin'))
    
    # Insert default inventory items if not exists
    default_items = [
        ('Room', 1200, 10, 'accommodation'),
        ('Pasta', 250, 50, 'food'),
        ('Burger', 120, 50, 'food'), 
        ('Noodles', 140, 50, 'food'),
        ('Shake', 120, 50, 'drink'),
        ('Chicken Roll', 150, 50, 'food')
    ]
    
    for item in default_items:
        cursor.execute("SELECT COUNT(*) FROM inventory WHERE name = ?", (item[0],))
        if cursor.fetchone()[0] == 0:
            cursor.execute("INSERT INTO inventory (name, price, quantity, category) VALUES (?, ?, ?, ?)", item)
    
    conn.commit()
    conn.close()

def authenticate_user(username, password):
    """Authenticate user and return user ID and role"""
    conn = sqlite3.connect('hotel.db')
    cursor = conn.cursor()
    
    password_hash = hashlib.md5(password.encode()).hexdigest()
    cursor.execute("SELECT id, role FROM users WHERE username = ? AND password = ?", 
                  (username, password_hash))
    result = cursor.fetchone()
    conn.close()
    
    return result if result else None

def get_inventory():
    """Get all inventory items"""
    conn = sqlite3.connect('hotel.db')
    df = pd.read_sql_query("SELECT * FROM inventory ORDER BY category, name", conn)
    conn.close()
    return df

def process_order(item_id, quantity, user_id):
    """Process an order with inventory update"""
    conn = sqlite3.connect('hotel.db')
    cursor = conn.cursor()
    
    try:
        # Begin transaction
        cursor.execute("BEGIN TRANSACTION")
        
        # Check current quantity
        cursor.execute("SELECT name, price, quantity FROM inventory WHERE id = ?", (item_id,))
        item_data = cursor.fetchone()
        
        if not item_data:
            return False, "Item not found"
        
        name, price, current_qty = item_data
        
        if current_qty < quantity:
            return False, f"Not enough inventory. Only {current_qty} available."
        
        # Update inventory
        new_qty = current_qty - quantity
        cursor.execute("UPDATE inventory SET quantity = ? WHERE id = ?", (new_qty, item_id))
        
        # Record sale
        total_price = price * quantity
        cursor.execute("INSERT INTO sales (item_id, quantity, total_price, user_id) VALUES (?, ?, ?, ?)",
                      (item_id, quantity, total_price, user_id))
        
        # Commit transaction
        cursor.execute("COMMIT")
        conn.close()
        
        return True, {
            'name': name,
            'quantity': quantity,
            'price': price,
            'total': total_price
        }
        
    except Exception as e:
        cursor.execute("ROLLBACK")
        conn.close()
        return False, str(e)

def get_daily_sales():
    """Get daily sales report"""
    conn = sqlite3.connect('hotel.db')
    query = '''
        SELECT i.name, i.category, SUM(s.quantity) as qty_sold, SUM(s.total_price) as revenue
        FROM sales s
        JOIN inventory i ON s.item_id = i.id
        WHERE DATE(s.timestamp) = DATE('now')
        GROUP BY s.item_id
        ORDER BY i.category, i.name
    '''
    df = pd.read_sql_query(query, conn)
    conn.close()
    return df

def get_all_sales():
    """Get all sales data for export"""
    conn = sqlite3.connect('hotel.db')
    query = '''
        SELECT s.timestamp, i.name, i.category, s.quantity, i.price, s.total_price, u.username
        FROM sales s
        JOIN inventory i ON s.item_id = i.id
        JOIN users u ON s.user_id = u.id
        WHERE DATE(s.timestamp) = DATE('now')
        ORDER BY s.timestamp
    '''
    df = pd.read_sql_query(query, conn)
    conn.close()
    return df

def add_user(username, password, role):
    """Add new user"""
    conn = sqlite3.connect('hotel.db')
    cursor = conn.cursor()
    
    try:
        password_hash = hashlib.md5(password.encode()).hexdigest()
        cursor.execute("INSERT INTO users (username, password, role) VALUES (?, ?, ?)",
                      (username, password_hash, role))
        conn.commit()
        conn.close()
        return True, "User added successfully"
    except sqlite3.IntegrityError:
        conn.close()
        return False, "Username already exists"
    except Exception as e:
        conn.close()
        return False, str(e)

# Initialize database
init_database()

# Session state initialization
if 'authenticated' not in st.session_state:
    st.session_state.authenticated = False
if 'user_id' not in st.session_state:
    st.session_state.user_id = None
if 'username' not in st.session_state:
    st.session_state.username = None
if 'role' not in st.session_state:
    st.session_state.role = None

# Login page
def login_page():
    st.markdown("# 🏨 Hotel Management System")
    st.markdown("---")
    
    col1, col2, col3 = st.columns([1, 2, 1])
    
    with col2:
        st.markdown("### 🔐 Login")
        
        with st.form("login_form"):
            username = st.text_input("Username", placeholder="Enter username")
            password = st.text_input("Password", type="password", placeholder="Enter password")
            login_button = st.form_submit_button("Login", use_container_width=True)
            
            if login_button:
                if username and password:
                    result = authenticate_user(username, password)
                    if result:
                        st.session_state.authenticated = True
                        st.session_state.user_id = result[0]
                        st.session_state.username = username
                        st.session_state.role = result[1]
                        st.success(f"Welcome, {username}!")
                        st.rerun()
                    else:
                        st.error("Invalid username or password")
                else:
                    st.error("Please enter both username and password")
        
        st.info("💡 Default login: *admin* / *admin123*")

# Main application
def main_app():
    # Sidebar
    st.sidebar.markdown(f"### Welcome, {st.session_state.username}!")
    st.sidebar.markdown(f"*Role:* {st.session_state.role.title()}")
    
    if st.sidebar.button("Logout", use_container_width=True):
        for key in st.session_state.keys():
            del st.session_state[key]
        st.rerun()
    
    st.sidebar.markdown("---")
    
    # Navigation
    if st.session_state.role == 'admin':
        menu_options = ["🛒 Place Order", "📊 Sales Report", "📦 Inventory Status", "👥 Manage Users", "📁 Export Data"]
    else:
        menu_options = ["🛒 Place Order", "📊 Sales Report", "📦 Inventory Status"]
    
    selected = st.sidebar.selectbox("Navigation", menu_options)
    
    # Main content
    st.markdown("# 🏨 Hotel Management System")
    
    if selected == "🛒 Place Order":
        place_order_page()
    elif selected == "📊 Sales Report":
        sales_report_page()
    elif selected == "📦 Inventory Status":
        inventory_status_page()
    elif selected == "👥 Manage Users" and st.session_state.role == 'admin':
        manage_users_page()
    elif selected == "📁 Export Data" and st.session_state.role == 'admin':
        export_data_page()

def place_order_page():
    st.markdown("## 🛒 Place Order")
    
    inventory_df = get_inventory()
    
    if inventory_df.empty:
        st.warning("No items available in inventory")
        return
    
    col1, col2 = st.columns([2, 1])
    
    with col1:
        st.markdown("### Available Items")
        
        # Display items by category
        categories = inventory_df['category'].unique()
        
        for category in categories:
            st.markdown(f"#### {category.title()}")
            category_items = inventory_df[inventory_df['category'] == category]
            
            for _, item in category_items.iterrows():
                with st.container():
                    item_col1, item_col2, item_col3, item_col4 = st.columns([3, 1, 1, 2])
                    
                    with item_col1:
                        st.write(f"{item['name']}")
                    with item_col2:
                        st.write(f"${item['price']}")
                    with item_col3:
                        st.write(f"Stock: {item['quantity']}")
                    with item_col4:
                        if item['quantity'] > 0:
                            if st.button(f"Order {item['name']}", key=f"order_{item['id']}"):
                                st.session_state.selected_item = item.to_dict()
                        else:
                            st.error("Out of Stock")
    
    with col2:
        if 'selected_item' in st.session_state:
            item = st.session_state.selected_item
            st.markdown("### Order Details")
            
            with st.form("order_form"):
                st.write(f"*Item:* {item['name']}")
                st.write(f"*Price:* ${item['price']}")
                st.write(f"*Available:* {item['quantity']}")
                
                quantity = st.number_input("Quantity", min_value=1, max_value=item['quantity'], value=1)
                total = quantity * item['price']
                st.write(f"*Total:* ${total}")
                
                if st.form_submit_button("Confirm Order", use_container_width=True):
                    success, result = process_order(item['id'], quantity, st.session_state.user_id)
                    
                    if success:
                        st.success("Order processed successfully!")
                        st.balloons()
                        
                        # Show bill
                        st.markdown("### 🧾 Bill Details")
                        st.write(f"*Item:* {result['name']}")
                        st.write(f"*Quantity:* {result['quantity']}")
                        st.write(f"*Price per item:* ${result['price']}")
                        st.write(f"*Total:* ${result['total']}")
                        
                        # Clear selection
                        del st.session_state.selected_item
                        st.rerun()
                    else:
                        st.error(f"Order failed: {result}")

def sales_report_page():
    st.markdown("## 📊 Daily Sales Report")
    
    sales_df = get_daily_sales()
    
    if sales_df.empty:
        st.info("No sales data for today")
        return
    
    col1, col2 = st.columns([3, 1])
    
    with col1:
        st.dataframe(sales_df, use_container_width=True)
    
    with col2:
        total_revenue = sales_df['revenue'].sum()
        total_items = sales_df['qty_sold'].sum()
        
        st.metric("Total Revenue", f"${total_revenue}")
        st.metric("Items Sold", total_items)
    
    # Charts
    if not sales_df.empty:
        col1, col2 = st.columns(2)
        
        with col1:
            st.markdown("### Revenue by Item")
            st.bar_chart(sales_df.set_index('name')['revenue'])
        
        with col2:
            st.markdown("### Quantity Sold by Category")
            category_sales = sales_df.groupby('category')['qty_sold'].sum()
            # st.pie_chart(category_sales.reset_index())


def inventory_status_page():
    st.markdown("## 📦 Current Inventory Status")
    
    inventory_df = get_inventory()
    
    if inventory_df.empty:
        st.warning("No inventory data available")
        return
    
    # Summary metrics
    col1, col2, col3, col4 = st.columns(4)
    
    with col1:
        st.metric("Total Items", len(inventory_df))
    with col2:
        st.metric("Categories", inventory_df['category'].nunique())
    with col3:
        st.metric("Low Stock Items", len(inventory_df[inventory_df['quantity'] <= 5]))
    with col4:
        st.metric("Out of Stock", len(inventory_df[inventory_df['quantity'] == 0]))
    
    # Inventory table
    st.dataframe(inventory_df, use_container_width=True)
    
    # Low stock alerts
    low_stock = inventory_df[inventory_df['quantity'] <= 5]
    if not low_stock.empty:
        st.warning("⚠ Low Stock Alert")
        st.dataframe(low_stock[['name', 'quantity', 'category']], use_container_width=True)

def manage_users_page():
    st.markdown("## 👥 User Management")
    
    with st.form("add_user_form"):
        st.markdown("### Add New User")
        
        col1, col2 = st.columns(2)
        
        with col1:
            new_username = st.text_input("Username")
            new_password = st.text_input("Password", type="password")
        
        with col2:
            new_role = st.selectbox("Role", ["staff", "admin"])
        
        if st.form_submit_button("Add User", use_container_width=True):
            if new_username and new_password:
                success, message = add_user(new_username, new_password, new_role)
                if success:
                    st.success(message)
                else:
                    st.error(message)
            else:
                st.error("Please fill all fields")

def export_data_page():
    st.markdown("## 📁 Export Sales Data")
    
    sales_df = get_all_sales()
    
    if sales_df.empty:
        st.info("No sales data to export for today")
        return
    
    st.markdown("### Today's Sales Data")
    st.dataframe(sales_df, use_container_width=True)
    
    # Convert to CSV
    csv_buffer = io.StringIO()
    sales_df.to_csv(csv_buffer, index=False)
    csv_data = csv_buffer.getvalue()
    
    # Download button
    filename = f"sales_report_{date.today().strftime('%Y%m%d')}.csv"
    st.download_button(
        label="Download CSV Report",
        data=csv_data,
        file_name=filename,
        mime="text/csv",
        use_container_width=True
    )

# Main app logic
if not st.session_state.authenticated:
    login_page()
else:
    main_app()