-- Sample data for testing (Updated schema)

-- Insert users with password_hash and is_online
INSERT INTO users (username, password_hash, is_online) VALUES 
('user1', '$2b$12$examplehash1', TRUE),
('user2', '$2b$12$examplehash2', FALSE),
('user3', '$2b$12$examplehash3', TRUE),
('admin', '$2b$12$examplehashadmin', TRUE)
ON CONFLICT (username) DO NOTHING;

-- Insert friends relationships (user_id, friend_id)
INSERT INTO friends (user_id, friend_id, status) VALUES 
(1, 2, 'accepted'),
(1, 3, 'pending'),
(2, 3, 'accepted'),
(2, 4, 'accepted')
ON CONFLICT (user_id, friend_id) DO NOTHING;

-- Insert groups (group_name, creator_id)
INSERT INTO groups (group_name, creator_id) VALUES 
('Developers', 1),
('Gamers', 2),
('Admins', 4)
ON CONFLICT DO NOTHING;

-- Insert group members
INSERT INTO group_members (group_id, user_id, role) VALUES 
(1, 1, 'admin'),
(1, 2, 'member'),
(1, 3, 'member'),
(2, 2, 'admin'),
(2, 3, 'member'),
(3, 4, 'admin'),
(3, 1, 'member')
ON CONFLICT (group_id, user_id) DO NOTHING;

-- Insert some sample messages
INSERT INTO messages (sender_id, receiver_id, group_id, content, is_delivered) VALUES 
-- Private messages
(1, 2, NULL, 'Hey user2, how are you?', TRUE),
(2, 1, NULL, 'I am good, thanks!', TRUE),
(1, 3, NULL, 'Are you online?', FALSE),
-- Group messages
(1, NULL, 1, 'Hello Developers group!', TRUE),
(2, NULL, 1, 'Hi everyone!', TRUE),
(2, NULL, 2, 'Who wants to play?', TRUE),
(4, NULL, 3, 'Admin meeting at 3pm', FALSE)
ON CONFLICT DO NOTHING;